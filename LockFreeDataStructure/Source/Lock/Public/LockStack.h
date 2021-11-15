#pragma once
#include "CommonUtil.h"
#include <mutex>


template<typename T>
class LockStack
{
public:
	void Push(T data)
	{
		std::lock_guard<std::mutex> lock_guard(m_lock);
		m_stack.push(data);
	}

	std::optional<T> Pop()
	{
		std::lock_guard<std::mutex> lock_guard(m_lock);
		if ( m_stack.empty() )
		{
			return {};
		}

		T data = m_stack.top();
		m_stack.pop();
		return data;
	}

private:
	std::stack<T> m_stack;
	std::mutex m_lock;
};