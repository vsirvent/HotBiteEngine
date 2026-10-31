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
#include <algorithm>
#include <map>
#include <iterator>
#include <numeric>
#include <utility>
#include <vector>

namespace HotBite {
    namespace Engine {
        namespace ECS {
            template <class T> class EntityVector {
            private:
                std::map<Entity, size_t> indexes;
                std::vector<T> data;
                // owners[i] is the entity data[i] belongs to: Remove moves the last entry into the freed slot and has to find that
                // entry's entity without walking `indexes` (a walk per Remove made hiding thousands of nodes quadratic).
                std::vector<Entity> owners;

            public:
                EntityVector() {
                    data.reserve(MAX_ENTITIES);
                    owners.reserve(MAX_ENTITIES);
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
                        owners.push_back(entity);
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
                        owners.push_back(entity);
                        index = data.size() - 1;
                        indexes[entity] = index;
                    }
                    return index;
                }

                // Puts the entries in the order `less` gives (on two entries) and keeps every entity pointing at its own entry.
                // Insert appends and Remove moves the last entry into the freed slot, so a system that must walk its
                // entities in an order of its own (the lock-step sim: by SimId) sorts after the set changes.
                template <class Less> void Sort(Less less) {
                    const size_t count = data.size();
                    const std::vector<Entity> owner = owners;
                    std::vector<size_t> order(count);
                    std::iota(order.begin(), order.end(), size_t(0));
                    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return less(data[a], data[b]); });
                    // In place: the buffer (reserved for MAX_ENTITIES, so that Insert never moves an entry) keeps its address.
                    std::vector<T> before(std::make_move_iterator(data.begin()), std::make_move_iterator(data.end()));
                    for (size_t i = 0; i < count; ++i) {
                        data[i] = std::move(before[order[i]]);
                        owners[i] = owner[order[i]];
                        indexes[owner[order[i]]] = i;
                    }
                }

                bool Remove(Entity entity) {
                    bool ret = false;
                    auto it = indexes.find(entity);
                    if (it != indexes.end()) {
                        if (indexes.size() == 1) {
                            indexes.clear();
                            data.clear();
                            owners.clear();
                        }
                        else {
                            size_t index_to_erase = it->second;
                            size_t index_to_move = data.size() - 1;
                            if (index_to_erase != index_to_move) {
                                const Entity key_to_move = owners[index_to_move];
                                data[index_to_erase] = data[index_to_move];
                                owners[index_to_erase] = key_to_move;
                                indexes[key_to_move] = index_to_erase;
                            }
                            data.pop_back();
                            owners.pop_back();
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
