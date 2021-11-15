#pragma once

using uint8 = unsigned char;
using uint32 = unsigned int;
using uint64 = unsigned long long;
using int32 = int;
using int64 = long long;


template<typename T32BITS, typename T64BITS, int PointerSize>
struct SelectIntPointerType
{
	// 부분특수화만 쓸거므로 에러나도록.
};

template<typename T32BITS, typename T64BITS>
struct SelectIntPointerType<T32BITS, T64BITS, 8>
{
	// 64비트
	typedef T64BITS TIntPointer;
};

template<typename T32BITS, typename T64BITS>
struct SelectIntPointerType<T32BITS, T64BITS, 4>
{
	// 32비트
	typedef T32BITS TIntPointer;
};

typedef SelectIntPointerType<uint32, uint64, sizeof(void*)>::TIntPointer UPTRINT;
typedef SelectIntPointerType<int32, int64, sizeof(void*)>::TIntPointer PTRINT;