cmake_minimum_required(VERSION 3.18)

project(LockFreeToy LANGUAGES CXX)

set(cmake_verbose_makefile true)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(LockFreeDataStructure)
add_subdirectory(UnitTest)
add_subdirectory(ThirdParty/benchmark EXCLUDE_FROM_ALL)