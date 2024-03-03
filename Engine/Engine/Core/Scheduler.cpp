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

#include "Scheduler.h"

using namespace HotBite::Engine::Core;

std::vector<Scheduler*> Scheduler::instances;
int64_t Scheduler::cycles_to_nsec = 0;

Scheduler::Scheduler() {
	InitTimer();
}

Scheduler::~Scheduler() {
	CloseHandle(htimer);
}

void Scheduler::Init(int count) {
	Release();
	instances.resize(count);
	for (int i = 0; i < count; ++i) {
		instances[i] = new Scheduler();		
	}
}

void Scheduler::Release() {
	for (int i = 0; i < instances.size(); ++i) {
		if (instances[i] != nullptr) {
			delete instances[i];			
		}
	}
	instances.clear();
}

Scheduler* Scheduler::Get(SchedulerId id) {
	return instances[id];
}

int64_t Scheduler::GetCycles()
{
	LARGE_INTEGER T1;
	QueryPerformanceCounter(&T1);
	return static_cast<int64_t>(T1.QuadPart);
}

void Scheduler::InitTimer() {
	LARGE_INTEGER Freq;
	TIMECAPS tc;
	for (int i = 0; i < MAX_TIMERS; ++i) {
		timer_id_counters.push((TimerId)i);
	}
	QueryPerformanceFrequency(&Freq);
	cycles_to_nsec = NSEC/(static_cast<int64_t>(Freq.QuadPart));
	t0 = GetCycles();
	htimer = CreateWaitableTimer(NULL, FALSE, NULL);
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
	{
		min_sleep_msec = tc.wPeriodMin;
		timeBeginPeriod(min_sleep_msec);
	}
	else {
		printf("InitTimer: Can't activate high resolution timers.\n");
	}
}

void Scheduler::NanoSleep(int64_t nsec) {
	LARGE_INTEGER li;
	li.QuadPart = -nsec / 100;
	if (!SetWaitableTimer(htimer, &li, 0, NULL, NULL, FALSE)) {
		printf("InitTimer: SetWaitableTimer failed.\n");
		return;
	}
	WaitForSingleObject(htimer, 100);
}

void Scheduler::Idle() {
	if (!timers.empty()) {
		int64_t sleep_time = min(timers.begin()->first - GetElapsedNanoSeconds() - MSEC_TO_NSEC(min_sleep_msec), MAX_SLEEP_NSEC_TIME);
		if (sleep_time > MSEC_TO_NSEC(min_sleep_msec + 2)) {
			NanoSleep(sleep_time);
		}
	}
}

int64_t Scheduler::GetNanoSeconds() {
	return ((Scheduler::GetCycles())) * cycles_to_nsec;
}

int64_t Scheduler::GetElapsedNanoSeconds() {
	return ((GetCycles() - t0)) * cycles_to_nsec;
}

int64_t Scheduler::GetElapsedMilliSeconds() {
	return NSEC_TO_MSEC(Scheduler::GetElapsedNanoSeconds());
}

int Scheduler::RegisterTimer(int64_t period_nsec, std::function<bool(const TimerData&)> cb) {
	TimerData td = {};
	if (cb != nullptr) {
		bool done = false;
		td.period = period_nsec;
		td.cb = cb;
		new_timer_mutex.lock();
		assert(!timer_id_counters.empty() && "max scheduler timers reached.");
		td.id = timer_id_counters.front();
		timer_id_counters.pop();
		new_timers.push_back(td);
		new_timer_mutex.unlock();
	}
	return td.id;
}

Scheduler::TimerId Scheduler::Exec(std::function<bool(const TimerData&)> cb) {
	return RegisterTimer(0, cb);
}


void Scheduler::_RegisterTimer(TimerData&& td) {
	bool done = false;
	//If we already have a timer with that period
	//add the callback to the timer
	for (auto timer = timers.begin(); timer != timers.end(); ++timer) {
		if (!timer->second.empty() && timer->second.front().period == td.period) {
			timer->second.push_back(td);
			td.total = timer->first;
			done = true;
			break;
		}
	}
	if (!done) {
		td.total = GetElapsedNanoSeconds();
		timers[td.total + td.period].emplace_back(td);
	}
}

void Scheduler::RemoveTimerAsync(Scheduler::TimerId id) {
	if (id != INVALID_TIMER_ID) {
		remove_timer_mutex.lock();
		removed_timers.push_back(id);
		remove_timer_mutex.unlock();
	}
}
 
bool Scheduler::RemoveTimer(Scheduler::TimerId id) {
	bool done = false;
	if (id != INVALID_TIMER_ID) {
		new_timer_mutex.lock();
		for (auto t = new_timers.begin(); t != new_timers.end(); ++t) {
			if (t->id == id) {
				new_timers.erase(t);
				done = true;
				break;
			}
		}
		new_timer_mutex.unlock();
		if (!done)
		{
			timer_mutex.lock();
			for (auto timer = timers.begin(); timer != timers.end(); ++timer) {
				for (auto timer_data = timer->second.begin(); timer_data != timer->second.end(); ++timer_data) {
					if (timer_data->id == id) {
						timer->second.erase(timer_data);
						done = true;
						break;
					}
				}
			}
			timer_mutex.unlock();
		}
		if (done) {
			new_timer_mutex.lock();
			timer_id_counters.push(id);
			new_timer_mutex.unlock();
		}
	}
	return done;
}

void Scheduler::Update() {
	remove_timer_mutex.lock();
	while (!removed_timers.empty()) {
		RemoveTimer(removed_timers.front());
		removed_timers.pop_front();
	}
	remove_timer_mutex.unlock();
	new_timer_mutex.lock();
	while (!new_timers.empty()) {
		_RegisterTimer(std::move(new_timers.front()));
		new_timers.pop_front();
	}
	new_timer_mutex.unlock();

	timer_mutex.lock();
	int64_t t = GetElapsedNanoSeconds();
	while (!timers.empty()) {
		std::map<int64_t, std::list<TimerData>>::iterator timer = timers.begin();
		if (timer->first < t) {
			for (auto timer_data = timer->second.begin(); timer_data != timer->second.end(); ++timer_data) {
				timer_data->elapsed = t - timer_data->total;
				timer_data->total = t;
				if (timer_data->cb(*timer_data)) {
					if (timer->first + timer_data->period < t) {
						timers[t].emplace_back(*timer_data);
					}
					else {
						timers[timer->first + timer_data->period].emplace_back(*timer_data);
					}
				}
				else {
					timer_id_counters.push(timer_data->id);
				}
			}
			timers.erase(timer);
		}
		else {
			break;
		}
	}
	timer_mutex.unlock();
}

void Scheduler::StartCount(Scheduler::CounterId count_id) {
	counters_mutex.lock();
	counters[count_id] = GetElapsedNanoSeconds();
	counters_mutex.unlock();
}

void Scheduler::RemoveCount(Scheduler::CounterId  count_id) {
	counters_mutex.lock();
	auto t = counters.find(count_id);
	if (t != counters.end()) {
		counters.erase(t);
	}
	counters_mutex.unlock();
}

int64_t Scheduler::GetCounterNanoseconds(Scheduler::CounterId  count_id) {
	int64_t ret = 0;
	counters_mutex.lock();
	auto t = counters.find(count_id);
	if (t != counters.end()) {
		ret = GetElapsedNanoSeconds() - t->second;
	}
	else {
		StartCount(count_id);
	}
	counters_mutex.unlock();
	return ret;
}
