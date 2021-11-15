#pragma once
#include <atomic>

#include "TypeDefines.h"


class ThreadSafeCounter
{
public:
	ThreadSafeCounter()
	{
		m_counter = 0;
	}
	
	// 다른 스레드의 카운터를 Copy 하기 위핸 복사생성자
	// 다른 스레드에서 Copy 해온 Counter 가, 다른 스레드에서 바뀌면 현 스레드에서 동기화되는 보장이 없다.
	// 알아서 잘 동기화 시켜줘라
	ThreadSafeCounter(const ThreadSafeCounter& other)
	{
		m_counter = other.GetValue();
	}

	ThreadSafeCounter(int32 value)
	{
		m_counter = value;
	}
	
	// New Value 반환
	int32 Increment()
	{
		return ++m_counter;
	}

	// Old Value 반환
	int32 Add(int32 amount)
	{
		return std::atomic_fetch_add(&m_counter, amount);
	}

	// New Value 반환
	int32 Decrement()
	{
		return --m_counter;
	}

	// Old Value 반환
	int32 Subtract(int32 amount)
	{
		return std::atomic_fetch_sub(&m_counter, amount);		
	}

	// Old Value 반환
	int32 Set(int32 value)
	{
		return std::atomic_exchange(&m_counter, value);
	}

	// Old Value 반환
	int32 Reset()
	{
		return std::atomic_exchange(&m_counter, 0);
	}

	// Cur Value 반환
	int32 GetValue() const
	{
		return m_counter.load();
	}

private:
	// thread safe 하도록 대입연산자 삭제
	void operator=(const ThreadSafeCounter& other) = delete;
	
	std::atomic<int32> m_counter;
};