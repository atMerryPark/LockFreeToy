set(HEADER)

set(SOURCE
	./Source/Private/Main.cpp
)


#enable_testing()

add_executable(UnitTest ${HEADER} ${SOURCE})
target_include_directories(UnitTest
PRIVATE
		../ThirdParty/benchmark/include/benchmark
		../LockFreeDataStructure/Source/Lock/Public
		../LockFreeDataStructure/Source/LockFree/Public
)

target_link_libraries(UnitTest benchmark_main LockFreeDataStructure)

 
#add_test(NAME UnitTest COMMAND UnitTest)