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
	
	// �ٸ� �������� ī���͸� Copy �ϱ� ���� ���������
	// �ٸ� �����忡�� Copy �ؿ� Counter ��, �ٸ� �����忡�� �ٲ�� �� �����忡�� ����ȭ�Ǵ� ������ ����.
	// �˾Ƽ� �� ����ȭ �������
	ThreadSafeCounter(const ThreadSafeCounter& other)
	{
		m_counter = other.GetValue();
	}

	ThreadSafeCounter(int32 value)
	{
		m_counter = value;
	}
	
	// New Value ��ȯ
	int32 Increment()
	{
		return ++m_counter;
	}

	// Old Value ��ȯ
	int32 Add(int32 amount)
	{
		return std::atomic_fetch_add(&m_counter, amount);
	}

	// New Value ��ȯ
	int32 Decrement()
	{
		return --m_counter;
	}

	// Old Value ��ȯ
	int32 Subtract(int32 amount)
	{
		return std::atomic_fetch_sub(&m_counter, amount);		
	}

	// Old Value ��ȯ
	int32 Set(int32 value)
	{
		return std::atomic_exchange(&m_counter, value);
	}

	// Old Value ��ȯ
	int32 Reset()
	{
		return std::atomic_exchange(&m_counter, 0);
	}

	// Cur Value ��ȯ
	int32 GetValue() const
	{
		return m_counter.load();
	}

private:
	// thread safe �ϵ��� ���Կ����� ����
	void operator=(const ThreadSafeCounter& other) = delete;
	
	std::atomic<int32> m_counter;
};