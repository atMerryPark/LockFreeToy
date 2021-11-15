#pragma once

#include "LockFreeCommon.h"


template<typename T>
class LockFreeStack : public LockFreeListLIFO<T>
{
};