#pragma once
#include "TypeDefines.h"
#include "ThreadSafeCounter.h"

#include <iostream>
#include <format>


template<class T, uint32  MaxTotalItems, uint32  ItemsPerPage, uint32 PaddingForCacheContention>
class TLockFreeFixedSizeMemoryPool
{
public:
	TLockFreeFixedSizeMemoryPool()
	{
		m_nextIndex.Increment();	// nullptr 스킵
		for ( uint32 i = 0; i < MaxBlocks; ++i )
		{
			m_pages[i] = nullptr;
		}
	};

	uint32 Alloc(uint32 count = 1)
	{
		uint32 firstItem = m_nextIndex.Add(count);

		if ( firstItem + count > MaxTotalItems )
		{	
			std::cout << std::format("Consumed {0} lock free links; there are no more.", MaxTotalItems) << std::endl;
			std::bad_alloc();
		}

		for ( uint32 curItem = firstItem; curItem < firstItem + count; ++curItem )
		{
			new (GetRawItem(curItem)) T();
		}

		return firstItem;
	}

	T* GetItem(uint32 index)
	{
		if ( !index )
		{
			return nullptr;
		}

		uint32 blockIndex = index / ItemsPerPage;
		uint32 offsetIndex = index % ItemsPerPage;

		check(index < static_cast<uint32>(m_nextIndex.GetValue()) && index < MaxTotalItems&& blockIndex < MaxBlocks&& m_pages[blockIndex]);

		return (m_pages[blockIndex] + offsetIndex);
	}

private:
	void* GetRawItem(uint32 index)
	{
		uint32 blockIndex = index / ItemsPerPage;
		uint32 offsetIndex = index % ItemsPerPage;
		check(index && index < static_cast<uint32>(m_nextIndex.GetValue()) && index < MaxTotalItems&& blockIndex < MaxBlocks);

		if ( m_pages[blockIndex] == nullptr )
		{
			T* newBlock = new T[ItemsPerPage];
			check(IsAligned(newBlock, alignof(T)));

			T* expected = nullptr;
			bool success = std::atomic_compare_exchange_strong(reinterpret_cast<std::atomic_intptr_t*>(&m_pages[blockIndex]), reinterpret_cast<std::intptr_t*>(&expected), *reinterpret_cast<std::intptr_t*>(&newBlock));
			
			if ( success == false )
			{
				check(m_pages[blockIndex] && m_pages[blockIndex] != newBlock);
				delete[] newBlock;
			}
			else
			{
				check(m_pages[blockIndex]);
			}
		}

		return static_cast<void*>(m_pages[blockIndex] + offsetIndex);
	}


private:
	// 최소 1 블럭 보장
	static constexpr uint32 MaxBlocks = (MaxTotalItems + ItemsPerPage - 1) / ItemsPerPage;


	uint8 padToAvoidContention0[PaddingForCacheContention];
	ThreadSafeCounter m_nextIndex;
	uint8 padToAvoidContention1[PaddingForCacheContention];
	T* m_pages[MaxBlocks] = { 0 };
	uint8 padToAvoidContention2[PaddingForCacheContention];
};
