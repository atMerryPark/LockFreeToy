set (Header
	./Source/Lock/Public/LockStack.h
	./Source/LockFree/Public/LockFreeStack.h
	./Source/LockFree/Public/TypeDefines.h
	./Source/LockFree/Public/CommonUtil.h

	./Source/LockFree/Private/LockFreeFixedSizeMemoryPool.h
	./Source/LockFree/Private/LockFreeCommon.h
	./Source/LockFree/Private/ThreadSafeCounter.h
)
 
set (SOURCE
	./Source/LockFree/Private/LockFreeCommon.cpp
	./Source/Lock/Private/LockStack.cpp
)

add_library(LockFreeDataStructure STATIC
	${Header}
	${SOURCE}
)

target_include_directories(LockFreeDataStructure
INTERFACE
	./Source/Lock/Public
	./Source/LockFree/Public/

PRIVATE
	./Source/Lock/Public
	./Source/LockFree/Public/
)