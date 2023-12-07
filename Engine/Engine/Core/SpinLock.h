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

#include <atomic>
#include <cassert>

namespace HotBite {
    namespace Engine {
        namespace Core {

            /**
             * Spin lock class, it follows naming convention of stl 
             * so we can switch between mutex to spin_locks.
             */
            class spin_lock {
            private:
                std::atomic<bool> l{ false };

            public:
                spin_lock(){}
                spin_lock(const spin_lock& other) {}
                spin_lock& operator=(const spin_lock& other) { return *this; }
                void lock() {
                    while (true) {
                        if (!l.exchange(true, std::memory_order_acquire)) {
                            return;
                        }
                        while (l.load(std::memory_order_relaxed)) {
                            _mm_pause();
                        }
                    }
                }

                bool try_lock() {
                    return !l.load(std::memory_order_relaxed) && !l.exchange(true, std::memory_order_acquire);
                }

                void unlock() {
                    l.store(false, std::memory_order_release);
                }
            };

            template <typename L>
            class AutoLock {
                L& l;
            public:
                AutoLock(L& lock) : l(lock) {
                    l.lock();
                }

                ~AutoLock() {
                    l.unlock();
                }
            };

            /**
             * Read shared/write exclusive spin lock class, it follows naming convention of stl.
             */
            class rw_spin_lock {
            private:
                static constexpr int SPIN_LOCK_UNLOCK = 0;
                static constexpr int SPIN_LOCK_WRITE_LOCK = -1;

                std::atomic<int> l;
            public:

                rw_spin_lock()
                {
                    std::atomic_store_explicit(&l, SPIN_LOCK_UNLOCK, std::memory_order_relaxed);
                }

                void read_lock()
                {
                    int expected;
                    int desired;

                    while (true)
                    {
                        expected = atomic_load_explicit(&l, std::memory_order_relaxed);
                        if (expected >= 0)
                        {
                            desired = 1 + expected;
                            if (atomic_compare_exchange_weak_explicit(&l, &expected, desired, std::memory_order_relaxed, std::memory_order_relaxed))
                                break; // success
                        }
                    }
                    std::atomic_thread_fence(std::memory_order_acquire); // sync
                }

                void read_unlock()
                {
                    int expected;
                    int desired;

                    while (true)
                    {
                        expected = atomic_load_explicit(&l, std::memory_order_relaxed);

                        if (expected > 0)
                        {
                            desired = expected - 1;

                            std::atomic_thread_fence(std::memory_order_release); // sync
                            if (atomic_compare_exchange_weak_explicit(&l, &expected, desired, std::memory_order_relaxed, std::memory_order_relaxed))
                                break; // success
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                }

                void write_lock()
                {
                    int expected;
                    int desired;

                    while (true)
                    {
                        expected = atomic_load_explicit(&l, std::memory_order_relaxed);

                        if (expected == SPIN_LOCK_UNLOCK)
                        {
                            desired = SPIN_LOCK_WRITE_LOCK;
                            if (atomic_compare_exchange_weak_explicit(&l, &expected, desired, std::memory_order_relaxed, std::memory_order_relaxed))
                                break; // success
                        }
                    }

                    std::atomic_thread_fence(std::memory_order_release); // sync
                }

                void write_unlock()
                {
                    int expected;
                    int desired;

                    while (true)
                    {
                        expected = atomic_load_explicit(&l, std::memory_order_relaxed);

                        if (expected == SPIN_LOCK_WRITE_LOCK)
                        {
                            desired = SPIN_LOCK_UNLOCK;

                            std::atomic_thread_fence(std::memory_order_release); // sync
                            if (atomic_compare_exchange_weak_explicit(&l, &expected, desired, std::memory_order_relaxed, std::memory_order_relaxed))
                                break; // success
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                }
            };
        }
    }
}
