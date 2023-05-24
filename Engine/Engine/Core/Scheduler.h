/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Defines.h"
#include "Utils.h"


#include <windows.h>
#include <inttypes.h>
#include <profileapi.h>
#include <unordered_map>
#include <map>
#include <mutex>
#include <functional>
#include <queue>
#include <Core/SpinLock.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

namespace HotBite {
	namespace Engine {
		namespace Core {

#define NSEC 1000000000LL
#define SEC_TO_NSEC(x) ((int64_t)(x)*NSEC)
#define NSEC_TO_SEC(x) ((int64_t)(x)/NSEC)
#define NSEC_TO_MSEC(x) ((int64_t)(x)/1000000LL)
#define MSEC_TO_NSEC(x) ((int64_t)(x)*1000000LL)

			/**
			 * Scheduler - RealTime high precission scheduler
			 *              providing timer callbacks and time counters.
			 * 
			 * This class is the core of the task management in the engine. 
			 * Every task is performed as a periodic task by this class and executed by a specific thread.
			 * Executing multithread scheduler is as easy as:
			 * 
			 * bool end = false;
			 * for (int i = 0; i < NTHREADS; ++i) {
					threads.emplace(std::thread([&e = this->end, i](){
						while (!e) {
							Scheduler::Get(i)->Update(); //Execute periodic tasks
							Scheduler::Get(i)->Idle();   //Idle until next period
						}
					}));			
				}
			 *
			 * Regitering period tasks example:
			 *
			 * Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(period_in_nsec, [](const Scheduler::TimerData& t) {
						//Do periodic tasks
						printf("Hello timer\n");
						//Return true to keep timer or false to remove timer from scheduler
						return true;
                    });
			 * 
			 */
			class Scheduler {
			public:
				//The timer data container
				struct TimerData {
					int64_t period;
					int64_t elapsed;
					int64_t total;
					int id;
					std::function<bool(const TimerData&)> cb;
				};

				using SchedulerId = int32_t;
				using TimerId = int32_t;
				using CounterId = int32_t;
				static constexpr TimerId INVALID_TIMER_ID = -1;
				static constexpr CounterId INVALID_COUNTER_ID = -1;
				static constexpr SchedulerId INVALID_SCHEDULER_ID = -1;

			private:
				static int constexpr MAX_SLEEP_NSEC_TIME = 100000000; //Maximum sleep 100 msec.
				static int constexpr MAX_TIMERS = 256;

				static std::vector<Scheduler*> instances;
				static int64_t cycles_to_nsec;
				static int64_t GetCycles();
				
				int64_t t0;
				int min_sleep_msec;
				std::unordered_map<CounterId, int64_t> counters;
				std::list<TimerData> new_timers;
				std::list<TimerId> removed_timers;
				std::map<int64_t, std::list<TimerData>> timers;
				std::queue<TimerId> timer_id_counters;
				
				std::recursive_mutex counters_mutex;
				spin_lock timer_mutex;
				spin_lock new_timer_mutex;
				
				HANDLE htimer;
				
				void InitTimer();
				Scheduler();
				virtual ~Scheduler();

				void _RegisterTimer(TimerData&& td);

			public:
				//Scheduler management
				static void Init(int count = 1);
				static Scheduler* Get(SchedulerId id = 0);
				static void Release();
				static int64_t GetNanoSeconds();

				//Tasks executions
				void Update();
				//Idle variable time until next timer
				void Idle();

				//Time functions				
				void NanoSleep(int64_t nsec);
				int64_t GetElapsedNanoSeconds();
				
				//Timers management
				TimerId RegisterTimer(int64_t period_nsec, std::function<bool(const TimerData&)> cb);
				bool RemoveTimer(TimerId id);
				void RemoveTimerAsync(TimerId id);

				//Counters management
				void StartCount(CounterId count_id);
				void RemoveCount(CounterId count_id);
				int64_t GetCounterNanoseconds(CounterId count_id);
			};
		}
	}
}