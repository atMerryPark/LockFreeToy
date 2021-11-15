#pragma once
#define NOMINMAX
#include <windows.h>
#include <assert.h>
#include <iostream>
#include <intrin.h>
#include <format>

#include "TypeDefines.h"
#include "ThreadSafeCounter.h"

using namespace std;


#define PLATFORM_CACHE_LINE_SIZE	64

#define MAX_LOCK_FREE_LINKS_AS_BITS (26)
#define MAX_LOCK_FREE_LINKS ( 1 << 26 )

#define check(expr) do \
		{ \
			if(!(expr)) \
			{ \
				__debugbreak(); \
			} \
		} while( false ) \

void LockFreeTagCounterHasOverflowed()
{
	assert(false);
	cout << "LockFree Tag has overflowed...(not a problem)." << endl;
	Sleep(1);
	__debugbreak();
}

void LockFreeLinksExhausted(uint32 TotalNum)
{
	assert(false);
	cout << format("Consumed {0} lock free links; there are no more.", TotalNum) << endl;
	__debugbreak();	
}

void* LockFreeAllocLinks(SIZE_T AllocSize);
void LockFreeFreeLinks(SIZE_T AllocSize, void* Ptr);

template <typename T>
FORCEINLINE constexpr bool IsAligned(T Val, uint64 Alignment)
{
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "�������̳� ���������� �;��Ѵ�.");

	return !((uint64)Val & (Alignment - 1));	// ������Ʈ�� ����ŷ�ؼ�, �ּҰ��� �ش� byte �� �� ���������� üũ
}

// Q: ������ 8����Ʈ ������� ( Ptrs ) �ۿ� ���µ� ���� alignas �� �ʿ� �ֳ�?
struct alignas(8) FIndexedPointer
{
	// no constructor, intentionally. We need to keep the ABA double counter in tact

	// This should only be used for FIndexedPointer's with no outstanding concurrency.
	// Not recycled links, for example.
	void Init()
	{
		static_assert(((MAX_LOCK_FREE_LINKS - 1) & MAX_LOCK_FREE_LINKS) == 0, "MAX_LOCK_FREE_LINKS �� 2�� �������� �Ѵ�.");
		Ptrs = 0;
	}
	__forceinline void SetAll(uint32 Ptr, uint64 CounterAndState)
	{
		check(Ptr < MAX_LOCK_FREE_LINKS&& CounterAndState < (uint64(1) << (64 - MAX_LOCK_FREE_LINKS_AS_BITS)));
		Ptrs = (uint64(Ptr) | (CounterAndState << MAX_LOCK_FREE_LINKS_AS_BITS));	// ������Ʈ�� Stamp , ������Ʈ�� Index
	}

	__forceinline uint32 GetPtr() const
	{
		return uint32(Ptrs & (MAX_LOCK_FREE_LINKS - 1));	// 25��° ��Ʈ���� 1 �� ä�� 2������ Mask �� ��
	}

	__forceinline void SetPtr(uint32 To)
	{
		SetAll(To, GetCounterAndState());
	}

	__forceinline uint64 GetCounterAndState() const
	{
		return (Ptrs >> MAX_LOCK_FREE_LINKS_AS_BITS);
	}

	__forceinline void SetCounterAndState(uint64 To)
	{
		SetAll(GetPtr(), To);
	}

	__forceinline void AdvanceCounterAndState(const FIndexedPointer& From, uint64 TABAInc)
	{
		SetCounterAndState(From.GetCounterAndState() + TABAInc);
		if ( GetCounterAndState() < From.GetCounterAndState() )
		{
			// �ε����� �����Ǵ°� ������� �������� �ʰ�, ���ϰ� �߻��ϴ� �ϵ� �ƴϴ�. �׸��� Sleep ���� �߰��� �صд�.
			// Q1: �� Sleep �� �� �Ǵ°ǰ�?
			// Q2: �ε��� ������ �� ���� ���� ����?
			LockFreeTagCounterHasOverflowed();
		}
	}

	// State : Stamp ��Ʈ���� ������Ʈ �Ϻκ��� �Ҵ��ؼ�, Ư�� '����' �� ǥ�� �� �� �ֵ��� �Ѵ�. ( enum �� �����Ѵٰ� �����ϸ� ���ҵ� )
	template<uint64 TABAInc>
	__forceinline uint64 GetState() const
	{
		return GetCounterAndState() & (TABAInc - 1);
	}

	template<uint64 TABAInc>
	__forceinline void SetState(uint64 Value)
	{
		check(Value < TABAInc);
		SetCounterAndState((GetCounterAndState() & ~(TABAInc - 1)) | Value);
	}

	__forceinline void AtomicRead(const FIndexedPointer& Other)
	{
		check(IsAligned(&Ptrs, 8) && IsAligned(&Other.Ptrs, 8));	// 8����Ʈ �޸� ���� �˻�

		volatile int64* Dest = (volatile int64*)&Other.Ptrs;
		if ( IsAligned(Dest, alignof(int64)) == false )
		{
			printf("InterlockedCompareExchange64 Dest ���� %d ����Ʈ�� ���ĵ��־�� �Ѵ�.", (int)alignof(int64));
			__debugbreak();
		}
		Ptrs = (int64)::_InterlockedCompareExchange64(Dest, 0, 0);

		// TestCriticalStall();		// Sleep() �� ���ؼ� ������ ������ ����Ī�� �߻���Ų��. ������ ��� ���¸� ���߽��Ѽ� livelock �� ã�´�.
	}

	__forceinline bool InterlockedCompareExchange(const FIndexedPointer& Exchange, const FIndexedPointer& Comparand)
	{
		// TestCriticalStall();
		return uint64((int64)::_InterlockedCompareExchange64((volatile int64*)&Ptrs, Exchange.Ptrs, Comparand.Ptrs)) == Comparand.Ptrs;		// Destination �� Old Value �� return �ϱ� ������ �̷��� �������� �Ǵ�.
	}

	__forceinline bool operator==(const FIndexedPointer& Other) const
	{
		return Ptrs == Other.Ptrs;
	}
	__forceinline bool operator!=(const FIndexedPointer& Other) const
	{
		return Ptrs != Other.Ptrs;
	}

private:
	uint64 Ptrs;
};


// ������ ��� �޸� �Ҵ�
template<class T, unsigned int MaxTotalItems, unsigned int ItemsPerPage>
class TLockFreeAllocOnceIndexedAllocator
{
	enum
	{
		// -1 : Zero ���� ( MaxTotalItems == ItemsPerPage )
		MaxBlocks = (MaxTotalItems + ItemsPerPage - 1) / ItemsPerPage
	};
public:

	TLockFreeAllocOnceIndexedAllocator()
	{
		NextIndex.Increment(); // skip nullptr
		for ( uint32 Index = 0; Index < MaxBlocks; Index++ )
		{
			Pages[Index] = nullptr;
		}
	}

	// �� ��� �Ҵ�
	__forceinline uint32 Alloc(uint32 Count = 1)
	{	
		// 1. Index �� ������Ű��, ��ȯ���� Old Value �̹Ƿ� FirstItem �� �̹��� �Ҵ��� ù ��° Index ���� ����Ų��.
		uint32 FirstItem = NextIndex.Add(Count);


		// 2. �޸� �� ��
		if ( FirstItem + Count > MaxTotalItems )
		{	
			LockFreeLinksExhausted(MaxTotalItems);
		}

		// 3. �޸� �����ؼ� new �Ҵ�
		for ( uint32 CurrentItem = FirstItem; CurrentItem < FirstItem + Count; CurrentItem++ )
		{
			new (GetRawItem(CurrentItem)) T();
		}

		return FirstItem;
	}

	// �ε����� �ش��ϴ� ��� ��ȯ
	__forceinline T* GetItem(uint32 Index)
	{
		if ( !Index )
		{
			return nullptr;
		}
		uint32 BlockIndex = Index / ItemsPerPage;	// �޸𸮺� ���
		uint32 SubIndex = Index % ItemsPerPage;		// �� �� offset ��

		// �ε��� Range Valid üũ, 
		check(Index < (uint32)NextIndex.GetValue() && Index < MaxTotalItems && BlockIndex < MaxBlocks && Pages[BlockIndex]);
		
		return Pages[BlockIndex] + SubIndex; // Block ���� �ּ� + offset 
	}

private:
	void* GetRawItem(uint32 Index)
	{
		uint32 BlockIndex = Index / ItemsPerPage;
		uint32 SubIndex = Index % ItemsPerPage;
		check(Index && Index < (uint32)NextIndex.GetValue() && Index < MaxTotalItems&& BlockIndex < MaxBlocks);

		// �޸𸮺��� ����ִ��� üũ
		if ( !Pages[BlockIndex] )
		{
			T* NewBlock = (T*)LockFreeAllocLinks(ItemsPerPage * sizeof(T));	// �޸𸮺� �Ҵ� ( �޸𸮺� = �� �� ���(Item) ����
			check(IsAligned(NewBlock, alignof(T)));		// ����� �޸����Ĵ�� �Ҵ����� üũ

			// ���� �Ҵ���� �ּҸ� �޸𸮺��� ��Ī��Ų��.
			void* Dest = &Pages[BlockIndex];
			if ( IsAligned(Dest, alignof(void*)) == false )
			{
				// HandleAtomicsFailure(TEXT("InterlockedCompareExchangePointer requires Dest pointer to be aligned to %d bytes"), (int)alignof(void*));
			}
			if ( ::InterlockedCompareExchangePointer((void**)&Dest, NewBlock, nullptr) != nullptr )
			{
				// �ٸ� �����忡�� �׻� �Ҵ��ص��ݾ�? ���� �Ҵ��� �޸𸮴� ��������
				check(Pages[BlockIndex] && Pages[BlockIndex] != NewBlock);
				LockFreeFreeLinks(ItemsPerPage * sizeof(T), NewBlock);
			}
			else
			{
				// �� �Ҵ� ����
				check(Pages[BlockIndex]);
			}
		}
		return (void*)(Pages[BlockIndex] + SubIndex);
	}

	// Cache Miss ���
	uint8 PadToAvoidContention0[PLATFORM_CACHE_LINE_SIZE];
	ThreadSafeCounter NextIndex;
	uint8 PadToAvoidContention1[PLATFORM_CACHE_LINE_SIZE];
	T* Pages[MaxBlocks];
	uint8 PadToAvoidContention2[PLATFORM_CACHE_LINE_SIZE];
};


// ���
struct FIndexedLockFreeLink
{
	FIndexedPointer DoubleNext;		// ���� ��� Index
	void* Payload;					// ��忡 ���� ������
	uint32 SingleNext;				// TODO: ?
};

// there is a version of this code that uses 128 bit atomics to avoid the indirection, that is why we have this policy class at all.
// Indirection( �������� ) ? �� ���ϱ� ���ؼ� 128��Ʈ Atomic �� ������ ����. �� ��å Ŭ������ �ִ� ����.
struct FLockFreeLinkPolicy
{
	enum
	{
		MAX_BITS_IN_TLinkPtr = MAX_LOCK_FREE_LINKS_AS_BITS
	};
	typedef FIndexedPointer TDoublePtr;
	typedef FIndexedLockFreeLink TLink;
	typedef uint32 TLinkPtr;
	typedef TLockFreeAllocOnceIndexedAllocator<FIndexedLockFreeLink, MAX_LOCK_FREE_LINKS, 16384> TAllocator;


	// TODO: ���⼭ ��ũ��, �ε���, Ptr �� �ǹ̰�?
	//		���⼱ Index �� Ptr �� �ǹ̴� ���ƺ��δ�. �Ѵ� Link �� ��ȯ�Ѵ�. �Լ��� �ܾ �°� ����.
	//		�ٸ� ������ �޶��� �� ������ ����ϴ°ǰ� ?
	//		Allocator Ÿ���� TLockFreeAllocOnceIndexedAllocator ���� �׷��ǰ�?

	// ��ũ ����
	static FORCEINLINE FIndexedLockFreeLink* DerefLink(uint32 Ptr)
	{
		return LinkAllocator.GetItem(Ptr);
	}
	static FORCEINLINE FIndexedLockFreeLink* IndexToLink(uint32 Index)
	{
		return LinkAllocator.GetItem(Index);
	}
	static FORCEINLINE uint32 IndexToPtr(uint32 Index)
	{
		return Index;
	}

	static TLinkPtr AllocLockFreeLink();
	static void FreeLockFreeLink(uint32 Item);
	static TAllocator LinkAllocator;
};



/*
* �������� root
//*/
//template<int TPaddingForCacheContention, uint64 TABAInc = 1>
//class FLockFreePointerListLIFORoot /*: public FNoncopyable*/
//{
//	typedef FLockFreeLinkPolicy::TDoublePtr TDoublePtr;
//	typedef FLockFreeLinkPolicy::TLink TLink;
//	typedef FLockFreeLinkPolicy::TLinkPtr TLinkPtr;
//
//public:
//	FORCEINLINE FLockFreePointerListLIFORoot()
//	{
//		// We want to make sure we have quite a lot of extra counter values to avoid the ABA problem. This could probably be relaxed, but eventually it will be dangerous. 
//		// The question is "how many queue operations can a thread starve for".
//		static_assert(MAX_TagBitsValue / TABAInc >= (1 << 23), "risk of ABA problem");
//		static_assert((TABAInc & (TABAInc - 1)) == 0, "must be power of two");
//		Reset();
//	}
//
//	void Reset()
//	{
//		Head.Init();
//	}
//
//	void Push(TLinkPtr Item)
//	{
//		while ( true )
//		{
//			TDoublePtr LocalHead;
//			LocalHead.AtomicRead(Head);
//			TDoublePtr NewHead;
//			NewHead.AdvanceCounterAndState(LocalHead, TABAInc);
//			NewHead.SetPtr(Item);
//			FLockFreeLinkPolicy::DerefLink(Item)->SingleNext = LocalHead.GetPtr();
//			if ( Head.InterlockedCompareExchange(NewHead, LocalHead) )
//			{
//				break;
//			}
//		}
//	}
//
//	bool PushIf(TFunctionRef<TLinkPtr(uint64)> AllocateIfOkToPush)
//	{
//		static_assert(TABAInc > 1, "method should not be used for lists without state");
//		while ( true )
//		{
//			TDoublePtr LocalHead;
//			LocalHead.AtomicRead(Head);
//			uint64 LocalState = LocalHead.GetState<TABAInc>();
//			TLinkPtr Item = AllocateIfOkToPush(LocalState);
//			if ( !Item )
//			{
//				return false;
//			}
//
//			TDoublePtr NewHead;
//			NewHead.AdvanceCounterAndState(LocalHead, TABAInc);
//			FLockFreeLinkPolicy::DerefLink(Item)->SingleNext = LocalHead.GetPtr();
//			NewHead.SetPtr(Item);
//			if ( Head.InterlockedCompareExchange(NewHead, LocalHead) )
//			{
//				break;
//			}
//		}
//		return true;
//	}
//
//
//	TLinkPtr Pop()
//	{
//		TLinkPtr Item = 0;
//		while ( true )
//		{
//			TDoublePtr LocalHead;
//			LocalHead.AtomicRead(Head);
//			Item = LocalHead.GetPtr();
//			if ( !Item )
//			{
//				break;
//			}
//			TDoublePtr NewHead;
//			NewHead.AdvanceCounterAndState(LocalHead, TABAInc);
//			TLink* ItemP = FLockFreeLinkPolicy::DerefLink(Item);
//			NewHead.SetPtr(ItemP->SingleNext);
//			if ( Head.InterlockedCompareExchange(NewHead, LocalHead) )
//			{
//				ItemP->SingleNext = 0;
//				break;
//			}
//		}
//		return Item;
//	}
//
//	TLinkPtr PopAll() TSAN_SAFE
//	{
//		TLinkPtr Item = 0;
//		while ( true )
//		{
//			TDoublePtr LocalHead;
//			LocalHead.AtomicRead(Head);
//			Item = LocalHead.GetPtr();
//			if ( !Item )
//			{
//				break;
//			}
//			TDoublePtr NewHead;
//			NewHead.AdvanceCounterAndState(LocalHead, TABAInc);
//			NewHead.SetPtr(0);
//			if ( Head.InterlockedCompareExchange(NewHead, LocalHead) )
//			{
//				break;
//			}
//		}
//		return Item;
//	}
//
//	TLinkPtr PopAllAndChangeState(TFunctionRef<uint64(uint64)> StateChange) TSAN_SAFE
//	{
//		static_assert(TABAInc > 1, "method should not be used for lists without state");
//		TLinkPtr Item = 0;
//		while ( true )
//		{
//			TDoublePtr LocalHead;
//			LocalHead.AtomicRead(Head);
//			Item = LocalHead.GetPtr();
//			TDoublePtr NewHead;
//			NewHead.AdvanceCounterAndState(LocalHead, TABAInc);
//			NewHead.SetState<TABAInc>(StateChange(LocalHead.GetState<TABAInc>()));
//			NewHead.SetPtr(0);
//			if ( Head.InterlockedCompareExchange(NewHead, LocalHead) )
//			{
//				break;
//			}
//		}
//		return Item;
//	}
//
//	FORCEINLINE bool IsEmpty() const
//	{
//		return !Head.GetPtr();
//	}
//
//	FORCEINLINE uint64 GetState() const
//	{
//		TDoublePtr LocalHead;
//		LocalHead.AtomicRead(Head);
//		return LocalHead.GetState<TABAInc>();
//	}
//
//private:
//
//	FPaddingForCacheContention<TPaddingForCacheContention> PadToAvoidContention1;
//	TDoublePtr Head;
//	FPaddingForCacheContention<TPaddingForCacheContention> PadToAvoidContention2;
//};




// TODO: ĳ�ö��� ���� ����
// TODO: �޸� ordering ����


// reference
// Interlocked API
//		������忡�� ���� �Ǳ� ������ �ſ� ������ ����//		
//		https://jungwoong.tistory.com/41

// For effective multithread programming
// https://bluekms21.gitbooks.io/femp/content/06_Non-BlockingAlgorithm.html

// volatile ��� ����
// 1. ����ȭ�� ���� ����.
// 2. caching �������� �޸𸮿� ���� operate �϶�.
// ��ó�� �������͸� �������� �ʰ� �ݵ�� �޸𸮸� ������ ��� ���ü�(visibility) �� ����ȴٰ� ���Ѵ�. ��Ƽ������ ���α׷��̶�� �� �����尡 �޸𸮿� �� ������ �ٸ� �����忡 ���δٴ� ���� �ǹ��Ѵ�.
// http://egloos.zum.com/sweeper/v/1781856
// https://skyul.tistory.com/337

// �޸� ���ü� �̶�?
// ��ó�� �������͸� �������� �ʰ� �ݵ�� �޸𸮸� ������ ��� ���ü�(visibility) �� ����ȴٰ� ���Ѵ�.��Ƽ������ ���α׷��̶�� �� �����尡 �޸𸮿� �� ������ �ٸ� �����忡 ���δٴ� ���� �ǹ��Ѵ�.


// TLS
// https://docs.microsoft.com/ko-kr/windows/win32/procthread/thread-local-storage
// https://en.wikipedia.org/wiki/Thread-local_storage#C_and_C++

// TLS �� ũ��� memory paging ������� �����ȴ�.

// TDB << TIB << TLS 
// Dll load �ÿ� TLS �� ��� �����Ǵ����� �Ӹ��ӿ� �� �ȱ׷�����.