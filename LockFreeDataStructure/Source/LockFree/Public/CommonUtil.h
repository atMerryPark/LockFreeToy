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
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "정수값이나 포인터형이 와야한다.");

	return !((uint64)Val & (Alignment - 1));	// 하위비트로 마스킹해서, 주소값이 해당 byte 로 딱 떨어지도록 체크
}