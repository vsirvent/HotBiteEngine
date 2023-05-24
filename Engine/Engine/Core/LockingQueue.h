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
#include <queue>
#include <mutex>
#include <condition_variable>
#include "SpinLock.h"

namespace HotBite {
    namespace Engine {
        namespace Core {

            template<typename T>
            class LockingQueue
            {
            public:
                void Push(T const& data)
                {
                    {
                        std::lock_guard<std::mutex> lock(guard);
                        queue.push(data);
                    }
                    signal.notify_one();
                }

                void Emplace(T && data)
                {
                    {
                        std::lock_guard<std::mutex> lock(guard);
                        queue.emplace(std::forward<T>(data));
                    }
                    signal.notify_one();
                }

                bool Empty() const
                {
                    std::lock_guard<std::mutex> lock(guard);
                    return queue.empty();
                }

                size_t Size() const
                {
                    std::lock_guard<std::mutex> lock(guard);
                    return queue.size();
                }

                T Pop()
                {
                    std::lock_guard<std::mutex> lock(guard);
                    T value = queue.front();
                    queue.pop();
                    return value;
                }

                bool TryPop(T& value)
                {
                    std::lock_guard<std::mutex> lock(guard);
                    if (queue.empty())
                    {
                        return false;
                    }

                    value = queue.front();
                    queue.pop();
                    return true;
                }

                void WaitAndPop(T& value)
                {
                    std::unique_lock<std::mutex> lock(guard);
                    while (queue.empty())
                    {
                        signal.wait(lock);
                    }

                    value = queue.front();
                    queue.pop();
                }

                bool TimedWaitAndPop(T& value, int milli)
                {
                    std::unique_lock<std::mutex> lock(guard);
                    while (queue.empty())
                    {
                        if (signal.wait_for(lock, std::chrono::milliseconds(milli)) == std::cv_status::timeout)
                        {
                            return false;
                        }
                    }
                    value = queue.front();
                    queue.pop();
                    return true;
                }

                bool TimedWait(int milli)
                {
                    std::unique_lock<std::mutex> lock(guard);
                    while (queue.empty())
                    {
                        if (signal.wait_for(lock, std::chrono::milliseconds(milli)) == std::cv_status::timeout)
                        {
                            return false;
                        }
                    }
                    return true;
                }

            private:
                std::queue<T> queue;
                mutable std::mutex guard;
                std::condition_variable signal;
            };

        }
    }
}