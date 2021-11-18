#include "LockFreeList.h"

#include <limits>

int main()
{
	cout << sizeof(long) << endl;
	cout << alignof(int32) << endl;
	
	int64 ii = std::numeric_limits<uint64>::max();			// overflow
	uint64 i = (int64)std::numeric_limits<uint64>::max();	// 
	cout << ii << endl;
	cout << i << endl;
	cout << std::numeric_limits<unsigned long long>::max() << endl;


	return 0;
}

void* LockFreeAllocLinks(SIZE_T AllocSize)
{	
	return malloc(AllocSize);	// UE4 는 TBB 씀
}

void LockFreeFreeLinks(SIZE_T AllocSize, void* Ptr)
{	
	return free(Ptr);	// UE4 는 TBB 씀
}

// 동적할당 한 메모리를 가리키는 Index 들을 TLS 에 저장한다.
// FreeList 를 사용해서, 빈 Index 가 있다면 재활용을 할 수 있도록 한다. ( Index 가 유한하기 때문 )
class LockFreeLinkAllocator_TLSCache
{
	enum
	{
		NUM_PER_BUNDLE = 64,
	};

	typedef FLockFreeLinkPolicy::TLink TLink;
	typedef FLockFreeLinkPolicy::TLinkPtr TLinkPtr;

public:
	LockFreeLinkAllocator_TLSCache()
	{	
		// check(IsInGameThread());

		TlsSlot = ::TlsAlloc();
		CheckValidTlsSlot();
	}

	~LockFreeLinkAllocator_TLSCache()
	{
		::TlsFree(TlsSlot);
		TlsSlot = 0;
	}

	
	LockFreeLinkAllocator_TLSCache(const LockFreeLinkAllocator_TLSCache&) = delete;
	LockFreeLinkAllocator_TLSCache& operator=(const LockFreeLinkAllocator_TLSCache&) = delete;
public:

	/**
	* Allocates a memory block of size SIZE.
	*
	* @return Pointer to the allocated memory.
	* @see Free
	*/
	TLinkPtr Pop()
	{
		FThreadLocalCache& TLS = GetTLS();

		if ( !TLS.PartialBundle )	// 한 번들을 다 쓰게되면 0 이됨.
		{
			if ( TLS.FullBundle )
			{
				TLS.PartialBundle = TLS.FullBundle;
				TLS.FullBundle = 0;
			}
			else
			{
				TLS.PartialBundle = GlobalFreeListBundles.Pop();	// FreeList 에서 빈 노드의 Index ( Allocator 메모리블럭의 SubIndex )
				if ( !TLS.PartialBundle )		// 재활용할게 없으면.
				{
					int32 FirstIndex = FLockFreeLinkPolicy::LinkAllocator.Alloc(NUM_PER_BUNDLE);	// 64개의 노드데이터가 할당됨. 여기서의 Index 는 Allocator 메모리블럭의 SubIndex
					for ( int32 Index = 0; Index < NUM_PER_BUNDLE; Index++ )
					{
						TLink* Event = FLockFreeLinkPolicy::IndexToLink(FirstIndex + Index);	// Link 가 노드
						Event->DoubleNext.Init();		// 노드초기화
						Event->SingleNext = 0;
						Event->Payload = (void*)UPTRINT(TLS.PartialBundle);		 // integer 포인터
						TLS.PartialBundle = FLockFreeLinkPolicy::IndexToPtr(FirstIndex + Index);
					}
				}
			}
			TLS.NumPartial = NUM_PER_BUNDLE;
		}
		TLinkPtr Result = TLS.PartialBundle;	// 현 Allocator 메모리블럭의 SubIndex
		TLink* ResultP = FLockFreeLinkPolicy::DerefLink(TLS.PartialBundle); // 현 노드
		TLS.PartialBundle = TLinkPtr(UPTRINT(ResultP->Payload));	// 포인터크기만큼 읽어와야 하므로. 현 노드의 전 SubIndex 를 세팅.	결국 멘탈적으로 뒤에 것부터 땡겨쓰겠다는 것.
		TLS.NumPartial--;
		//checkLockFreePointerList(TLS.NumPartial >= 0 && ((!!TLS.NumPartial) == (!!TLS.PartialBundle)));
		ResultP->Payload = nullptr;
		
		check(!ResultP->DoubleNext.GetPtr() && !ResultP->SingleNext);		// 비어있는지 확인.
		return Result;
	}

	/**
	* Puts a memory block previously obtained from Allocate() back on the free list for future use.
	*
	* @param Item The item to free.
	* @see Allocate
	*/
	void Push(TLinkPtr Item)
	{
		FThreadLocalCache& TLS = GetTLS();
		if ( TLS.NumPartial >= NUM_PER_BUNDLE )		// 한 번들이 아예 텅 비게되었는가?
		{
			if ( TLS.FullBundle )
			{
				GlobalFreeListBundles.Push(TLS.FullBundle);
				//TLS.FullBundle = nullptr;
			}
			TLS.FullBundle = TLS.PartialBundle;		// 번들의 시작경계지점.
			TLS.PartialBundle = 0;
			TLS.NumPartial = 0;
		}
		TLink* ItemP = FLockFreeLinkPolicy::DerefLink(Item);
		ItemP->DoubleNext.SetPtr(0);
		ItemP->SingleNext = 0;
		ItemP->Payload = (void*)UPTRINT(TLS.PartialBundle);		// FreeList 에서는, 다음 거를 가리킨다.
		TLS.PartialBundle = Item;
		TLS.NumPartial++;
	}

private:

	/** struct for the TLS cache. */
	struct FThreadLocalCache
	{
		TLinkPtr FullBundle;
		TLinkPtr PartialBundle;		// Allocator 메모리블러그이 SubIndex. 현 TLS 캐싱에서, Current Index 를 의미
		int32 NumPartial;

		FThreadLocalCache()
			: FullBundle(0)
			, PartialBundle(0)
			, NumPartial(0)
		{
		}
	};

	FThreadLocalCache& GetTLS()
	{
		CheckValidTlsSlot();
		FThreadLocalCache* TLS = (FThreadLocalCache*)::TlsGetValue(TlsSlot);
		if ( !TLS )
		{
			TLS = new FThreadLocalCache();
			::TlsSetValue(TlsSlot, TLS);
		}
		return *TLS;
	}

	void CheckValidTlsSlot() const
	{
		check(TlsSlot != TLS_OUT_OF_INDEXES);
	}

	/** Slot for TLS struct. */
	uint32 TlsSlot;

	/** Lock free list of free memory blocks, these are all linked into a bundle of NUM_PER_BUNDLE. */
	FLockFreePointerListLIFORoot<PLATFORM_CACHE_LINE_SIZE> GlobalFreeListBundles;
};


static LockFreeLinkAllocator_TLSCache GLockFreeLinkAllocator;

FLockFreeLinkPolicy::TLinkPtr FLockFreeLinkPolicy::AllocLockFreeLink()
{
	FLockFreeLinkPolicy::TLinkPtr Result = GLockFreeLinkAllocator.Pop();

	// this can only really be a mem stomp	// 이게 메모리 쿵쾅(?) Stomp 일 수 있다.	
	check(Result && !FLockFreeLinkPolicy::DerefLink(Result)->DoubleNext.GetPtr() && !FLockFreeLinkPolicy::DerefLink(Result)->Payload && !FLockFreeLinkPolicy::DerefLink(Result)->SingleNext);	// 노드 데이터가 모두 유효한지 체크. TLS 메모리 할당 됬는지 체크
	return Result;
}

void FLockFreeLinkPolicy::FreeLockFreeLink(uint32 Item)
{
	GLockFreeLinkAllocator.Push(Item);	// 할당했던 메모리블럭을 재사용하기 위해서, Free List 에 반환.
}