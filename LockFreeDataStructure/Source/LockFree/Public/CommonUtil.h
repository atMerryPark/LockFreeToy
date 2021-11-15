#pragma once
#include "TypeDefines.h"


#define check(expr) do \
		{ \
			if(!(expr)) \
			{ \
				__debugbreak(); \
			} \
		} while( false ) \



template <typename T>
__forceinline constexpr bool IsAligned(T Val, uint64 Alignment)
{
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "�������̳� ���������� �;��Ѵ�.");

	return !((uint64)Val & (Alignment - 1));	// ������Ʈ�� ����ŷ�ؼ�, �ּҰ��� �ش� byte �� �� ���������� üũ
}