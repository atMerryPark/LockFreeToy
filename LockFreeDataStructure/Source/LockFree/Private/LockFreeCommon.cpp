
#include "LockFreeCommon.h"


LockFreeLinkPolicy::LockFreeMemoryPool LockFreeLinkPolicy::m_linkAllocator;

template<uint32 PaddingForCacheContention = LOCKFREE_CACHELINE_SIZE>
class LockFreeLinkFreeList
{
public:

	void Push(uint32 index)
	{
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

	uint32 Pop()
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

			IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(index);
			newHead.Set(link->m_nextIndex, curHead.GetStamp() + 1);

			if ( m_head.CompareExchange(curHead, newHead) )
			{
				link->m_nextIndex = 0;
				break;
			}
		}

		return index;
	}


private:
	uint8 padding0[PaddingForCacheContention] = { 0 };
	StampedIndex m_head;
	uint8 padding1[PaddingForCacheContention] = { 0 };
};

class LockFreeLinkAllocator_TLSBase
{
public:
	uint32 Alloc()
	{
		ThreadLocalCache& cache = GetTLS();
		
		if ( !cache.m_partialBundle )	// 부분번들 다 씀
		{
			if ( cache.m_fullBundle )
			{
				cache.m_partialBundle = cache.m_fullBundle;
				cache.m_fullBundle = 0;
			}
			else
			{
				cache.m_partialBundle = m_freeListBundles.Pop();
				if ( !cache.m_partialBundle )
				{
					uint32 baseIndex = LockFreeLinkPolicy::LinkAllocator().Alloc(NumPerBundle);
					for ( uint32 i = 0; i < NumPerBundle; ++i )
					{
						IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(baseIndex + i);
						link->m_nextIndex = 0;
						link->m_payload = (void*)(UPTRINT(cache.m_partialBundle));
						cache.m_partialBundle = baseIndex + i;
					}
				}
			}
			cache.m_numPartial = NumPerBundle;
		}

		uint32 resultIndex = cache.m_partialBundle;
		IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(resultIndex);
		
		cache.m_partialBundle = (uint32)(UPTRINT(link->m_payload));
		--cache.m_numPartial;

		link->m_payload = nullptr;

		return resultIndex;
	}

	void Dealloc(uint32 index)
	{
		ThreadLocalCache cache = GetTLS();
		if ( cache.m_numPartial >= NumPerBundle )
		{
			if ( cache.m_fullBundle )
			{
				m_freeListBundles.Push(cache.m_fullBundle);
			}

			cache.m_fullBundle = cache.m_partialBundle;
			cache.m_partialBundle = 0;
			cache.m_numPartial = 0;
		}

		IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(index);
		link->m_nextStampedIndex.SetIndex(0);	// stamp 는 그대로.
		link->m_nextIndex = 0;
		link->m_payload = (void*)UPTRINT(cache.m_partialBundle);

		cache.m_partialBundle = index;
		++cache.m_numPartial;
	}


public:
	LockFreeLinkAllocator_TLSBase() = default;
	~LockFreeLinkAllocator_TLSBase()
	{
		m_tlsSlot = 0;
	}

	LockFreeLinkAllocator_TLSBase(const LockFreeLinkAllocator_TLSBase&) = delete;
	LockFreeLinkAllocator_TLSBase(LockFreeLinkAllocator_TLSBase&&) = delete;
	LockFreeLinkAllocator_TLSBase& operator=(const LockFreeLinkAllocator_TLSBase&) = delete;
	LockFreeLinkAllocator_TLSBase& operator=(LockFreeLinkAllocator_TLSBase&&) = delete;

private:
	struct ThreadLocalCache
	{
		uint32 m_fullBundle = 0;
		uint32 m_partialBundle = 0;
		uint32 m_numPartial = 0;
	};

	ThreadLocalCache& GetTLS()
	{
		if ( m_tlsSlot == UnintializedSlot )
		{
			m_tlsSlot = m_tlsCache.size();
			m_tlsCache.emplace_back();
		}

		return m_tlsCache[m_tlsSlot];
	}	

	static constexpr uint32 NumPerBundle = 64;
	static constexpr std::size_t UnintializedSlot = (std::numeric_limits<std::size_t>::max)();

	static thread_local std::size_t m_tlsSlot;
	static thread_local std::vector<ThreadLocalCache> m_tlsCache;

	LockFreeLinkFreeList<> m_freeListBundles;
};

thread_local std::vector<LockFreeLinkAllocator_TLSBase::ThreadLocalCache> LockFreeLinkAllocator_TLSBase::m_tlsCache;
thread_local std::size_t LockFreeLinkAllocator_TLSBase::m_tlsSlot = LockFreeLinkAllocator_TLSBase::UnintializedSlot;



static LockFreeLinkAllocator_TLSBase g_LockFreeListAllocator;

uint32 LockFreeLinkPolicy::AllocLockFreeLink()
{
	uint32 index = g_LockFreeListAllocator.Alloc();

	IndexedLockFreeLink* link = LockFreeLinkPolicy::IndexToLink(index);
	check(index && link && link->m_nextStampedIndex.GetIndex() == 0 && link->m_payload == nullptr && link->m_nextIndex == 0);
	return index;
}

void LockFreeLinkPolicy::DeallocLockFreeLink(uint32 index)
{
	g_LockFreeListAllocator.Dealloc(index);
}