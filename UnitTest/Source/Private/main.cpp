#include "benchmark.h"

#include "LockFreeStack.h"
#include "LockStack.h"

#include <iostream>
#include <thread>
#include <vector>
#include <windows.h>


#define DECLARE_BENCH(func) \
static void BM_##func(benchmark::State& state)	\
{												\
	for ( auto _ : state )						\
	{											\
		##func(state.range(0), state);			\
	}											\
}												\
BENCHMARK(BM_##func)							\
->RangeMultiplier(2)->Range(1 << 0, 1 << 4)		\
->Unit(BenchMarkTimeUnit)						\
->Iterations(BenchMarkIteration)				\
->MeasureProcessCPUTime();						\


#define EXIT_ERROR(exp) \
	do \
	{  \
		if ( (##exp) == false )		\
		{				\
			state.SkipWithError("Sync Error");	\
			return;		\
		}	\
		else\
		{	\
			break;	\
		}	\
	} while ( true );	\
	


namespace
{
	constexpr uint32 BenchMarkIteration = 2;
	constexpr benchmark::TimeUnit BenchMarkTimeUnit = benchmark::kMillisecond;

	constexpr uint32 LoopCount = 8'000'000;
}


//////////////////////////
//		 Lock
//////////////////////////

void LockStackPush(int threadNum, benchmark::State& state)
{
	LockStack<int> ls;

	std::atomic<std::size_t> count = 0;

	auto job = [&]()
	{
		for ( int i = 0; i < LoopCount; ++i )
		{
			ls.Push(i);
			++count;
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);
	
	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job));
	}

	for ( auto& thread : threads )
	{
		thread.join();
	}

	state.PauseTiming();
	while ( true )
	{
		if ( ls.Pop() )
		{
			--count;
		}
		else
		{
			break;
		}
	}
	state.ResumeTiming();
	
	
	EXIT_ERROR(count == 0);
}

void LockStackPop(int threadNum, benchmark::State& state)
{
	LockStack<int> ls;

	uint64 total = (uint64)(LoopCount) * (uint64)threadNum;
	std::atomic<std::size_t> count = total;

	state.PauseTiming();
	for ( uint64 i = 0; i < total; ++i )
	{
		ls.Push(i);
	}
	state.ResumeTiming();

	

	auto job = [&]()
	{
		for ( int i = 0; i < LoopCount; ++i )
		{
			if ( ls.Pop() )
			{
				--count;
			}
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);

	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job));
	}

	for ( auto& thread : threads )
	{
		thread.join();
	}


	EXIT_ERROR(count == 0);	
}

void LockStackPushPop(int threadNum, benchmark::State& state)
{
	LockStack<int> ls;

	std::atomic<std::size_t> count = 0;

	auto job = [&](int tid)
	{
		for ( int i = 0; i < LoopCount; ++i )
		{
			if ( rand() % 2 == 0 )
			{
				if ( ls.Pop() )
				{
					--count;
				}
			}
			else
			{
				ls.Push(i);
				++count;
			}
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);

	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job, i));
	}

	for ( auto& thread : threads )
	{
		thread.join();
	}


	state.PauseTiming();
	std::size_t total = 0;
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
	state.ResumeTiming();


	EXIT_ERROR(count == total);
}


//////////////////////////
//		 LockFree
//////////////////////////

void LockFreeStackPush(int threadNum, benchmark::State& state)
{
	LockFreeStack<int> ls;

	std::atomic<std::size_t> count = 0;

	auto job = [&]()
	{
		for ( int i = 1; i <= LoopCount; ++i )
		{
			ls.Push((int*)i);
			++count;
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);
	
	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job));
	}

	for ( auto& thread : threads )
	{
		thread.join();
	}


	std::size_t total = 0;

	state.PauseTiming();	
	while ( true )
	{
		if ( ls.Pop() )
		{
			--count;
		}
		else
		{
			break;
		}
	}	
	state.ResumeTiming();


	EXIT_ERROR(count == 0);
}

void LockFreeStackPop(int threadNum, benchmark::State& state)
{
	LockFreeStack<int> ls;

	uint64 total = (uint64)(LoopCount) * (uint64)threadNum;
	std::atomic<std::size_t> count = total;

	state.PauseTiming();
	for ( uint64 i = 1; i <= total; ++i )
	{
		ls.Push((int*)i);
	}
	state.ResumeTiming();


	auto job = [&]()
	{
		for ( int i = 0; i < LoopCount; ++i )
		{
			if ( ls.Pop() )
			{
				--count;
			}
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);

	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job));
	}

	for ( auto& thread : threads )
	{
		thread.join();
	}


	EXIT_ERROR(count == 0);
}

void LockFreeStackPushPop(int threadNum, benchmark::State& state)
{	
	LockFreeStack<int> ls;

	std::atomic<std::size_t> count = 0;

	auto job = [&](int tid)
	{
		for ( int i = 1; i <= LoopCount; ++i )
		{
			if ( rand() % 2 == 0 )
			{
				if ( ls.Pop() )
				{
					--count;
				}
			}
			else
			{
				ls.Push((int*)i);
				++count;
			}
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(threadNum);

	for ( int i = 0; i < threadNum; ++i )
	{
		threads.emplace_back(std::thread(job, i));
	}	

	for ( auto& thread : threads )
	{
		thread.join();
	}


	state.PauseTiming();
	std::size_t total = 0;
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
	state.ResumeTiming();

	
	EXIT_ERROR(total == count)
}




DECLARE_BENCH(LockStackPush)
DECLARE_BENCH(LockStackPop)
DECLARE_BENCH(LockStackPushPop)

DECLARE_BENCH(LockFreeStackPush)
DECLARE_BENCH(LockFreeStackPop)
DECLARE_BENCH(LockFreeStackPushPop)




BENCHMARK_MAIN();