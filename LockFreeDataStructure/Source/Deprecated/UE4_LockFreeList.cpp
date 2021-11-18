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
	return malloc(AllocSize);	// UE4 �� TBB ��
}

void LockFreeFreeLinks(SIZE_T AllocSize, void* Ptr)
{	
	return free(Ptr);	// UE4 �� TBB ��
}

// �����Ҵ� �� �޸𸮸� ����Ű�� Index ���� TLS �� �����Ѵ�.
// FreeList �� ����ؼ�, �� Index �� �ִٸ� ��Ȱ���� �� �� �ֵ��� �Ѵ�. ( Index �� �����ϱ� ���� )
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

		if ( !TLS.PartialBundle )	// �� ������ �� ���ԵǸ� 0 �̵�.
		{
			if ( TLS.FullBundle )
			{
				TLS.PartialBundle = TLS.FullBundle;
				TLS.FullBundle = 0;
			}
			else
			{
				TLS.PartialBundle = GlobalFreeListBundles.Pop();	// FreeList ���� �� ����� Index ( Allocator �޸𸮺��� SubIndex )
				if ( !TLS.PartialBundle )		// ��Ȱ���Ұ� ������.
				{
					int32 FirstIndex = FLockFreeLinkPolicy::LinkAllocator.Alloc(NUM_PER_BUNDLE);	// 64���� ��嵥���Ͱ� �Ҵ��. ���⼭�� Index �� Allocator �޸𸮺��� SubIndex
					for ( int32 Index = 0; Index < NUM_PER_BUNDLE; Index++ )
					{
						TLink* Event = FLockFreeLinkPolicy::IndexToLink(FirstIndex + Index);	// Link �� ���
						Event->DoubleNext.Init();		// ����ʱ�ȭ
						Event->SingleNext = 0;
						Event->Payload = (void*)UPTRINT(TLS.PartialBundle);		 // integer ������
						TLS.PartialBundle = FLockFreeLinkPolicy::IndexToPtr(FirstIndex + Index);
					}
				}
			}
			TLS.NumPartial = NUM_PER_BUNDLE;
		}
		TLinkPtr Result = TLS.PartialBundle;	// �� Allocator �޸𸮺��� SubIndex
		TLink* ResultP = FLockFreeLinkPolicy::DerefLink(TLS.PartialBundle); // �� ���
		TLS.PartialBundle = TLinkPtr(UPTRINT(ResultP->Payload));	// ������ũ�⸸ŭ �о�;� �ϹǷ�. �� ����� �� SubIndex �� ����.	�ᱹ ��Ż������ �ڿ� �ͺ��� ���ܾ��ڴٴ� ��.
		TLS.NumPartial--;
		//checkLockFreePointerList(TLS.NumPartial >= 0 && ((!!TLS.NumPartial) == (!!TLS.PartialBundle)));
		ResultP->Payload = nullptr;
		
		check(!ResultP->DoubleNext.GetPtr() && !ResultP->SingleNext);		// ����ִ��� Ȯ��.
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
		if ( TLS.NumPartial >= NUM_PER_BUNDLE )		// �� ������ �ƿ� �� ��ԵǾ��°�?
		{
			if ( TLS.FullBundle )
			{
				GlobalFreeListBundles.Push(TLS.FullBundle);
				//TLS.FullBundle = nullptr;
			}
			TLS.FullBundle = TLS.PartialBundle;		// ������ ���۰������.
			TLS.PartialBundle = 0;
			TLS.NumPartial = 0;
		}
		TLink* ItemP = FLockFreeLinkPolicy::DerefLink(Item);
		ItemP->DoubleNext.SetPtr(0);
		ItemP->SingleNext = 0;
		ItemP->Payload = (void*)UPTRINT(TLS.PartialBundle);		// FreeList ������, ���� �Ÿ� ����Ų��.
		TLS.PartialBundle = Item;
		TLS.NumPartial++;
	}

private:

	/** struct for the TLS cache. */
	struct FThreadLocalCache
	{
		TLinkPtr FullBundle;
		TLinkPtr PartialBundle;		// Allocator �޸𸮺����� SubIndex. �� TLS ĳ�̿���, Current Index �� �ǹ�
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

	// this can only really be a mem stomp	// �̰� �޸� ����(?) Stomp �� �� �ִ�.	
	check(Result && !FLockFreeLinkPolicy::DerefLink(Result)->DoubleNext.GetPtr() && !FLockFreeLinkPolicy::DerefLink(Result)->Payload && !FLockFreeLinkPolicy::DerefLink(Result)->SingleNext);	// ��� �����Ͱ� ��� ��ȿ���� üũ. TLS �޸� �Ҵ� ����� üũ
	return Result;
}

void FLockFreeLinkPolicy::FreeLockFreeLink(uint32 Item)
{
	GLockFreeLinkAllocator.Push(Item);	// �Ҵ��ߴ� �޸𸮺��� �����ϱ� ���ؼ�, Free List �� ��ȯ.
}