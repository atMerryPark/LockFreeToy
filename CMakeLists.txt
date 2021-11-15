cmake_minimum_required(VERSION 3.18)

project(LockFreeToy LANGUAGES CXX)

set(cmake_verbose_makefile true)

set(CMAKE_CXX_STANDARD 17)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(LockFreeDataStructure)
add_subdirectory(UnitTest)
add_subdirectory(ThirdParty/benchmark EXCLUDE_FROM_ALL)