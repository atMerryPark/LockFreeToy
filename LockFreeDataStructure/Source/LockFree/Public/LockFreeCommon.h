#pragma once
#include <atomic>
#include <limits>
#include <vector>


#include "CommonUtil.h"
#include "TypeDefines.h"
#include "LockFreeFixedSizeMemoryPool.h"


constexpr uint32 LOCKFREE_CACHELINE_SIZE = 64;	// bytes

struct alignas(8) StampedIndex
{
public:

	void Set(uint32 index, uint64 stamp)
	{
		check(index < IndexMax && stamp < (uint64(1) << (std::numeric_limits<uint64>::digits - IndexBits)));
		m_stampedIndex = static_cast<uint64>(index) | (stamp << IndexBits);
	}

	void SetStamp(uint64 stamp)
	{
		Set(GetIndex(), stamp);
	}

	uint64 GetStamp() const
	{
		return (m_stampedIndex >> IndexBits);
	}

	uint32 GetIndex() const
	{
		return static_cast<uint32>(m_stampedIndex & IndexBitMask);
	}

	void SetIndex(uint32 index)
	{
		Set(index, GetStamp());
	}
	
	bool CompareExchange(const StampedIndex& expected, const StampedIndex& desired)
	{
		uint64 expectedStampedIndex = expected.m_stampedIndex;
		return std::atomic_compare_exchange_strong(reinterpret_cast<std::atomic_llong*>(this), reinterpret_cast<int64*>(&expectedStampedIndex), desired.m_stampedIndex);
	}


public:
	constexpr StampedIndex()
	{
		static_assert(((IndexMax - 1) & IndexMax) == 0, "IndexMax 는 2의 지수여야 한다.");
	}
	StampedIndex(const StampedIndex& other)
	{
		(*this) = other;
	}
	StampedIndex& operator=(const StampedIndex& other)
	{
		if ( this != &other )
		{
			m_stampedIndex = std::atomic_load(reinterpret_cast<const std::atomic_llong*>(&other));
		}

		return *this;
	}
	StampedIndex(StampedIndex&& other) = delete;
	StampedIndex& operator=(StampedIndex&& other) = delete;

	__forceinline bool operator==(const StampedIndex& other) const
	{
		return (*this).m_stampedIndex == other.m_stampedIndex;
	}

	__forceinline bool operator!=(const StampedIndex& other) const
	{
		return !((*this).m_stampedIndex == other.m_stampedIndex);
	}

public:
	static constexpr uint32 IndexBits = (31);
	static constexpr uint32 IndexMax = (1 << IndexBits);
	static constexpr uint32 IndexBitMask = (IndexMax - 1);

private:
	uint64 m_stampedIndex = 0;
};


struct IndexedLockFreeLink
{
	StampedIndex m_nextStampedIndex;
	void* m_payload		= nullptr;
	uint32 m_nextIndex	= 0;
};


struct LockFreeLinkPolicy
{
public:
	static constexpr uint32 MaxLockFreeLink = StampedIndex::IndexMax;

	using LockFreeMemoryPool = TLockFreeFixedSizeMemoryPool<IndexedLockFreeLink, MaxLockFreeLink, 16384, LOCKFREE_CACHELINE_SIZE>;


	__forceinline static IndexedLockFreeLink* IndexToLink(uint32 index)
	{
		return m_linkAllocator.GetItem(index);
	}

	static uint32 AllocLockFreeLink();
	static void DeallocLockFreeLink(uint32 index);

	static LockFreeMemoryPool& LinkAllocator() { return m_linkAllocator; }		
private:
	static LockFreeMemoryPool m_linkAllocator;
};


template<typename T, uint32 PaddingForCacheContention = LOCKFREE_CACHELINE_SIZE>
class LockFreeListLIFO
{
public:

	void Push(T* data)
	{
		uint32 index = LockFreeLinkPolicy::AllocLockFreeLink();
		LockFreeLinkPolicy::IndexToLink(index)->m_payload = data;

		while ( true )
		{
			StampedIndex curHead = m_head;
			StampedIndex newHead;
			newHead.Set(index, curHead.GetStamp() + 1);

			LockFreeLinkPolicy::IndexToLink(index)->m_nextIndex = curHead.GetIndex();
			if ( m_head.CompareExchange(curHead, newHead) )
			{
				break;
			}
		}
	}

	T* Pop()
	{
		uint32 index = 0;
		T* data = nullptr;
		while ( true )
		{
			StampedIndex curHead = m_head;
			index = curHead.GetIndex();
			if ( index = 0 )
			{
				break;
			}

			StampedIndex newHead;

			IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(index);
			newHead.Set(link->m_nextIndex, curHead.GetStamp() + 1);
			
			if ( m_head.CompareExchange(curHead, newHead) )
			{
				data = static_cast<T*>(link->m_payload);
				LockFreeLinkPolicy::DeallocLockFreeLink(index);
				break;
			}
		}
		
		return data;
	}

	void PopAll(std::vector<T*>& OutVector)
	{
		uint32 index = 0;
		while ( true )
		{
			StampedIndex curHead = m_head;
			index = curHead.GetIndex();
			if ( index == 0 )
			{
				break;
			}

			StampedIndex newHead;
			newHead.Set(0, curHead.GetStamp() + 1);

			if ( m_head.CompareExchange(curHead, newHead) )
			{
				break;
			}
		}

		while ( index )
		{
			IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(index);
			OutVector.emplace_back(static_cast<T*>(link->m_payload));
			
			uint32 del = index;
			index = link->m_nextIndex;

			LockFreeLinkPolicy::DeallocLockFreeLink(del);
		}
	}

	bool IsEmpty() const
	{
		return (m_head.GetIndex() == 0);
	}

private:	
	uint8 padding1[PaddingForCacheContention] = { 0 };
	StampedIndex m_head;
	uint8 padding2[PaddingForCacheContention] = { 0 };
};