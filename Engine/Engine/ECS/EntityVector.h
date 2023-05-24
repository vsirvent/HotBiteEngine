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

#include "Types.h"
#include <map>
#include <utility>

namespace HotBite {
    namespace Engine {
        namespace ECS {
            template <class T> class EntityVector {
            private:
                std::map<Entity, size_t> indexes;
                std::vector<T> data;

            public:
                EntityVector() {
                    data.reserve(MAX_ENTITIES);
                }

                std::vector<T>& GetData() { return data; }
                const std::vector<T>& GetConstData() const { return data; }

                T* Get(Entity entity)  {
                    T* ret = nullptr;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        ret = &(data.at(it->second));
                    }
                    return ret;
                }
                
                const T* Get(Entity entity) const {
                    const T* ret = nullptr;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        ret = &(data.at(it->second));
                    }
                    return ret;
                }

                size_t Insert(Entity entity, const T& entry) {
                    size_t index;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        index = it->second;
                        data[index] = entry;
                    }
                    else {
                        data.push_back(entry);
                        index = data.size() - 1;
                        indexes[entity] = index;
                    }
                    return index;
                }

                size_t Insert(Entity entity, T&& entry) {
                    size_t index;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        index = it->second;
                        data[index] = entry;
                    }
                    else {
                        data.emplace_back(std::forward<T>(entry));
                        index = data.size() - 1;
                        indexes[entity] = index;
                    }
                    return index;
                }

                bool Remove(Entity entity) {
                    bool ret = false;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        if (indexes.size() == 1) {
                            indexes.clear();
                            data.clear();
                        }
                        else {
                            size_t index_to_erase = it->second;
                            size_t index_to_move = data.size() - 1;
                            Entity key_to_move;
                            for (auto& it_to_move : indexes) {
                                if (it_to_move.second == index_to_move) {
                                    key_to_move = it_to_move.first;
                                    break;
                                }
                            }
                            if (index_to_erase != data.size() - 1) {
                                data[index_to_erase] = data[index_to_move];
                                indexes[key_to_move] = index_to_erase;
                            }
                            data.pop_back();
                            indexes.erase(it);
                            ret = true;
                        }
                    }
                    return ret;
                }
            };
        }
    }
}
