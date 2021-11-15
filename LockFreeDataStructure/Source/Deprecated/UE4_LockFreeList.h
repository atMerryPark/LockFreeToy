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
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "정수값이나 포인터형이 와야한다.");

	return !((uint64)Val & (Alignment - 1));	// 하위비트로 마스킹해서, 주소값이 해당 byte 로 딱 떨어지도록 체크
}

// Q: 어차피 8바이트 멤버변수 ( Ptrs ) 밖에 없는데 굳이 alignas 쓸 필요 있나?
struct alignas(8) FIndexedPointer
{
	// no constructor, intentionally. We need to keep the ABA double counter in tact

	// This should only be used for FIndexedPointer's with no outstanding concurrency.
	// Not recycled links, for example.
	void Init()
	{
		static_assert(((MAX_LOCK_FREE_LINKS - 1) & MAX_LOCK_FREE_LINKS) == 0, "MAX_LOCK_FREE_LINKS 는 2의 지수여야 한다.");
		Ptrs = 0;
	}
	__forceinline void SetAll(uint32 Ptr, uint64 CounterAndState)
	{
		check(Ptr < MAX_LOCK_FREE_LINKS&& CounterAndState < (uint64(1) << (64 - MAX_LOCK_FREE_LINKS_AS_BITS)));
		Ptrs = (uint64(Ptr) | (CounterAndState << MAX_LOCK_FREE_LINKS_AS_BITS));	// 상위비트가 Stamp , 하위비트가 Index
	}

	__forceinline uint32 GetPtr() const
	{
		return uint32(Ptrs & (MAX_LOCK_FREE_LINKS - 1));	// 25번째 비트까지 1 로 채운 2진수를 Mask 로 씀
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
			// 인덱싱이 오버되는게 문제라고 생각하지 않고, 흔하게 발생하는 일도 아니다. 그리고 Sleep 으로 추가방어를 해둔다.
			// Q1: 왜 Sleep 이 방어가 되는건가?
			// Q2: 인덱싱 오버가 왜 문제 되지 않지?
			LockFreeTagCounterHasOverflowed();
		}
	}

	// State : Stamp 비트에서 하위비트 일부분을 할당해서, 특정 '상태' 를 표현 할 수 있도록 한다. ( enum 을 저장한다고 생각하면 편할듯 )
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
		check(IsAligned(&Ptrs, 8) && IsAligned(&Other.Ptrs, 8));	// 8바이트 메모리 정렬 검사

		volatile int64* Dest = (volatile int64*)&Other.Ptrs;
		if ( IsAligned(Dest, alignof(int64)) == false )
		{
			printf("InterlockedCompareExchange64 Dest 값은 %d 바이트로 정렬되있어야 한다.", (int)alignof(int64));
			__debugbreak();
		}
		Ptrs = (int64)::_InterlockedCompareExchange64(Dest, 0, 0);

		// TestCriticalStall();		// Sleep() 을 통해서 강제로 스레드 스위칭을 발생시킨다. 스레드 기아 상태를 유발시켜서 livelock 을 찾는다.
	}

	__forceinline bool InterlockedCompareExchange(const FIndexedPointer& Exchange, const FIndexedPointer& Comparand)
	{
		// TestCriticalStall();
		return uint64((int64)::_InterlockedCompareExchange64((volatile int64*)&Ptrs, Exchange.Ptrs, Comparand.Ptrs)) == Comparand.Ptrs;		// Destination 의 Old Value 를 return 하기 때문에 이렇게 성공여부 판단.
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


// 락프리 노드 메모리 할당
template<class T, unsigned int MaxTotalItems, unsigned int ItemsPerPage>
class TLockFreeAllocOnceIndexedAllocator
{
	enum
	{
		// -1 : Zero 방지 ( MaxTotalItems == ItemsPerPage )
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

	// 새 노드 할당
	__forceinline uint32 Alloc(uint32 Count = 1)
	{	
		// 1. Index 는 증가시키고, 반환값은 Old Value 이므로 FirstItem 은 이번에 할당할 첫 번째 Index 값을 가리킨다.
		uint32 FirstItem = NextIndex.Add(Count);


		// 2. 메모리 블럭 고갈
		if ( FirstItem + Count > MaxTotalItems )
		{	
			LockFreeLinksExhausted(MaxTotalItems);
		}

		// 3. 메모리 지정해서 new 할당
		for ( uint32 CurrentItem = FirstItem; CurrentItem < FirstItem + Count; CurrentItem++ )
		{
			new (GetRawItem(CurrentItem)) T();
		}

		return FirstItem;
	}

	// 인덱스에 해당하는 노드 반환
	__forceinline T* GetItem(uint32 Index)
	{
		if ( !Index )
		{
			return nullptr;
		}
		uint32 BlockIndex = Index / ItemsPerPage;	// 메모리블럭 계산
		uint32 SubIndex = Index % ItemsPerPage;		// 블럭 내 offset 값

		// 인덱스 Range Valid 체크, 
		check(Index < (uint32)NextIndex.GetValue() && Index < MaxTotalItems && BlockIndex < MaxBlocks && Pages[BlockIndex]);
		
		return Pages[BlockIndex] + SubIndex; // Block 시작 주소 + offset 
	}

private:
	void* GetRawItem(uint32 Index)
	{
		uint32 BlockIndex = Index / ItemsPerPage;
		uint32 SubIndex = Index % ItemsPerPage;
		check(Index && Index < (uint32)NextIndex.GetValue() && Index < MaxTotalItems&& BlockIndex < MaxBlocks);

		// 메모리블럭이 비어있는지 체크
		if ( !Pages[BlockIndex] )
		{
			T* NewBlock = (T*)LockFreeAllocLinks(ItemsPerPage * sizeof(T));	// 메모리블럭 할당 ( 메모리블럭 = 블럭 당 노드(Item) 개수
			check(IsAligned(NewBlock, alignof(T)));		// 노드의 메모리정렬대로 할당됬는지 체크

			// 새로 할당받은 주소를 메모리블럭에 매칭시킨다.
			void* Dest = &Pages[BlockIndex];
			if ( IsAligned(Dest, alignof(void*)) == false )
			{
				// HandleAtomicsFailure(TEXT("InterlockedCompareExchangePointer requires Dest pointer to be aligned to %d bytes"), (int)alignof(void*));
			}
			if ( ::InterlockedCompareExchangePointer((void**)&Dest, NewBlock, nullptr) != nullptr )
			{
				// 다른 스레드에서 그새 할당해뒀잖아? 새로 할당한 메모리는 해제하자
				check(Pages[BlockIndex] && Pages[BlockIndex] != NewBlock);
				LockFreeFreeLinks(ItemsPerPage * sizeof(T), NewBlock);
			}
			else
			{
				// 블럭 할당 성공
				check(Pages[BlockIndex]);
			}
		}
		return (void*)(Pages[BlockIndex] + SubIndex);
	}

	// Cache Miss 방어
	uint8 PadToAvoidContention0[PLATFORM_CACHE_LINE_SIZE];
	ThreadSafeCounter NextIndex;
	uint8 PadToAvoidContention1[PLATFORM_CACHE_LINE_SIZE];
	T* Pages[MaxBlocks];
	uint8 PadToAvoidContention2[PLATFORM_CACHE_LINE_SIZE];
};


// 노드
struct FIndexedLockFreeLink
{
	FIndexedPointer DoubleNext;		// 다음 노드 Index
	void* Payload;					// 노드에 담을 데이터
	uint32 SingleNext;				// TODO: ?
};

// there is a version of this code that uses 128 bit atomics to avoid the indirection, that is why we have this policy class at all.
// Indirection( 간접참조 ) ? 를 피하기 위해서 128비트 Atomic 을 연산을 쓴다. 이 정책 클래스가 있는 이유.
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


	// TODO: 여기서 링크와, 인덱스, Ptr 의 의미가?
	//		여기선 Index 와 Ptr 의 의미는 같아보인다. 둘다 Link 를 반환한다. 함수명만 단어에 맞게 맞춤.
	//		다른 곳에선 달라질 수 있음을 대비하는건가 ?
	//		Allocator 타입이 TLockFreeAllocOnceIndexedAllocator 여서 그런건가?

	// 링크 참조
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
* 락프리의 root
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




// TODO: 캐시라인 문제 공부
// TODO: 메모리 ordering 공부


// reference
// Interlocked API
//		유저모드에서 동작 되기 때문에 매우 빠르게 동작//		
//		https://jungwoong.tistory.com/41

// For effective multithread programming
// https://bluekms21.gitbooks.io/femp/content/06_Non-BlockingAlgorithm.html

// volatile 사용 이유
// 1. 최적화를 하지 마라.
// 2. caching 하지말고 메모리에 직접 operate 하라.
// 이처럼 레지스터를 재사용하지 않고 반드시 메모리를 참조할 경우 가시성(visibility) 이 보장된다고 말한다. 멀티쓰레드 프로그램이라면 한 쓰레드가 메모리에 쓴 내용이 다른 쓰레드에 보인다는 것을 의미한다.
// http://egloos.zum.com/sweeper/v/1781856
// https://skyul.tistory.com/337

// 메모리 가시성 이란?
// 이처럼 레지스터를 재사용하지 않고 반드시 메모리를 참조할 경우 가시성(visibility) 이 보장된다고 말한다.멀티쓰레드 프로그램이라면 한 쓰레드가 메모리에 쓴 내용이 다른 쓰레드에 보인다는 것을 의미한다.


// TLS
// https://docs.microsoft.com/ko-kr/windows/win32/procthread/thread-local-storage
// https://en.wikipedia.org/wiki/Thread-local_storage#C_and_C++

// TLS 의 크기는 memory paging 기법으로 관리된다.

// TDB << TIB << TLS 
// Dll load 시에 TLS 가 어떻게 구성되는지가 머릿속에 잘 안그려진다.