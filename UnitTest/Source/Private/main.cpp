#include "benchmark.h"

#include "LockFreeStack.h"
#include "LockStack.h"

#include <iostream>
#include <thread>



constexpr uint32 LoopCount = 10'000;

static void BM_LockStackPush(benchmark::State& state)
{
	for ( auto _ : state )
	{
		LockStack<int> ls;

		auto job = [&]()
		{
			for ( int i = 0; i < LoopCount; ++i )
			{
				ls.Push(i);
			}
		};

		std::thread threads[12];

		auto start = std::chrono::system_clock::now();
		for ( auto& thread : threads )
		{
			thread = std::thread(job);
		}

		for ( auto& thread : threads )
		{
			thread.join();
		}
		auto end = std::chrono::system_clock::now();
		// std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << std::endl;

		// state.PauseTiming();
		/*std::size_t total = 0;
		while ( true )
		{
			if ( ls.Pop() )
			{
				++total;
			}
			else
			{
				break;
			}
		}

		assert(total == LoopCount * 8);*/
		// state.ResumeTiming();
	}
}
BENCHMARK(BM_LockStackPush);
//->Complexity(benchmark::oLogN);

static void BM_LockFreeStackPush(benchmark::State& state)
{
	for ( auto _ : state )
	{
		LockFreeStack<int> ls;

		auto job = [&]()
		{
			for ( int i = 1; i <= LoopCount; ++i )
			{
				ls.Push((int*)i);
			}
		};

		std::thread threads[12];

		auto start = std::chrono::system_clock::now();
		for ( auto& thread : threads )
		{
			thread = std::thread(job);
		}

		for ( auto& thread : threads )
		{
			thread.join();
		}
		auto end = std::chrono::system_clock::now();
		//std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << std::endl;

		// state.PauseTiming();
		/*std::size_t total = 0;
		while ( true )
		{
			if ( ls.Pop() )
			{
				++total;
			}
			else
			{
				break;
			}
		}

		assert(total == LoopCount * 8);*/
		//state.ResumeTiming();
	}
}
BENCHMARK(BM_LockFreeStackPush);
//->Complexity(benchmark::oLogN);

BENCHMARK_MAIN();