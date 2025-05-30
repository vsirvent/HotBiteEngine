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

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include "Defines.h"
#include <d3d11.h>
#include <atomic>
#include <string>
#include <DirectXMath.h>

namespace HotBite {
	namespace Engine {
		namespace Core {

            #define RELEASE_PTR(x) { if (x != nullptr) { x->Release(); x = nullptr; }}
            #define DELETE_PTR(x) { if (x != nullptr) { delete x; x = nullptr; }}
            #define RGA_NOISE_W 256
            #define RGBA_NOISE_H 256
            #define RGBA_NOISE_LEN (RGA_NOISE_W*RGBA_NOISE_H*4)
            extern const uint8_t rgba_noise_map[RGBA_NOISE_LEN];
#define DISTF2_2(p) (p.x * p.x + p.y * p.y)
#define DIST2(p) (p.x * p.x + p.y * p.y + p.z * p.z)

            float3 parseColorStringF3(const std::string& colorString);
            float4 parseColorStringF4(const std::string& colorString);

            void SetFlag(uint32_t& bitset, uint32_t flag);
            void ClearFlag(uint32_t& bitset, uint32_t flag);
            void UpdateFlag(uint32_t& bitset, uint32_t flag, bool active);

            bool Match(const std::string& str, const std::string& pattern);

            template <typename T> int sgn(T val) {
                return (T(0) < val) - (val < T(0));
            }

            float4 ColorFromStr(const std::string& color);
            float3 ColorRGBFromStr(const std::string& color);
            float2 WorldToScreen(const float3& world_position, const matrix& view_projection, const float w, const float h);
            std::vector<std::vector<uint8_t>> getBlackAndWhiteBmp(std::string filename);
            float getRandomNumber(float min, float max);

            template <class T>
            uint64_t GetTypeId() {
                return (uint64_t)(typeid(T).hash_code());
            }

            template <class T, class V>
            V GetId(T* obj, uint32_t id) {
                return (V)((uint64_t)obj & 0xffff0000 | id & 0x0000ffff);
            }

            template <typename T>
            class RandType {
            private:
                T vmin{};
                T vmax{};

            public:

                RandType() {}
                RandType(const T& min_val, const T& max_val) :vmin(min_val), vmax(max_val) {
                }

                bool Init() {
                    return vmin != vmax;
                }

                T Value() const {
                    return (T)(vmin + (((T)std::rand()) * (vmax - vmin)) / (T)RAND_MAX);
                }
            };

            
            /**
             * This is an map that stores data in vectors in order to allow fast access and
             * and be cache friendly.
             */
            template <class K, class T>
            class FlatMap {
            private:
                std::map<K, size_t> indexes;
                std::vector<T> data;
                
            public:
                std::vector<T>& GetData() { return data; }

                FlatMap() {
                }
                
                FlatMap(int initial_size) {
                    data.reserve(initial_size);
                }

                //Clear the map
                void Clear() {
                    data.clear();
                    indexes.clear();
                }

                //Create default value by key
                T* Create(const K& k) {
                    if (Get(k) == nullptr) {
                        Insert(k, T{});
                    }
                    return Get(k);
                }

                //Get value by key
                T* Get(const K& k) const {
                    T* ret = nullptr;
                    auto it = indexes.find(k);
                    if (it != indexes.end()) {
                        ret = (T*)&data.at(it->second);
                    }
                    return ret;
                }

                //Get matched values by string key
                std::list<T*> GetStrMatch(const std::string& k) {
                    std::list<T*> ret;
                    if (k.find("*") != std::string::npos) {
                        for (auto& i : indexes) {
                            if (Match(i.first, k)) {
                                ret.push_back(&data[i.second]);
                            }
                        }
                    }
                    else {
                        auto it = indexes.find(k);
                        if (it != indexes.end()) {
                            ret.push_back(&data[it->second]);
                        }
                    }
                    return ret;
                }

                //Insert entry in the map
                size_t Insert(const K& k, const T& v) {
                    size_t index;
                    auto it = indexes.find(k);
                    if (it != indexes.end()) {
                        data[it->second] = v;
                        index = it->second;
                    }
                    else {
                        data.push_back(v);
                        index = data.size() - 1;
                        indexes[k] = index;
                    }
                    return index;
                }

                //Remove entry from the map
                size_t Insert(const K& k, T&& entry) {
                    size_t index;
                    auto it = indexes.find(k);
                    if (it != indexes.end()) {
                        data[it->second] = entry;
                        index = it->second;
                    }
                    else {
                        data.emplace_back(std::forward<T>(entry));
                        index = data.size() - 1;
                        indexes[k] = index;
                    }
                    return index;
                }

                //For removing an entry we remove always last
                //position of the vector and move it to the 
                //erased entry to avoid moving all the vector data
                bool Remove(const K& k) {
                    bool ret = false;
                    auto it = indexes.find(k);
                    if (it != indexes.end()) {
                        //If just one entry clear everything
                        if (indexes.size() == 1) {
                            Clear();
                        }
                        else {
                            size_t index_to_erase = it->second;
                            size_t index_to_move = data.size() - 1;
                            K& key_to_move;
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
                        }
                    }
                    return ret;
                }
            };          

            /**
             * This is an vector with no position order.
             */
            template <class T>
            class UnorderedVector {
            private:
                std::vector<T> data;

            public:
                std::vector<T>& GetData() { return data; }

                UnorderedVector() {
                }

                UnorderedVector(int initial_size) {
                    data.reserve(initial_size);
                }

                //Clear the map
                void Clear() {
                    data.clear();
                }

                //Get value by key
                T& Get(int index) {
                    return data[index];
                }

                //Insert entry in the vector
                void Insert(const T& v) {
                    return data.push_back(v);
                }

                //For removing an entry we remove always last
                //position of the vector and move it to the 
                //erased entry to avoid moving all the vector data
                bool Remove(int index) {
                    bool ret = false;
                    //If just one entry clear everything
                    if (data.size() == 1) {
                        Clear();
                    }
                    else {
                        size_t index_to_erase = index;
                        size_t index_to_move = data.size() - 1;
                        if (index_to_erase != data.size() - 1) {
                            data[index_to_erase] = data[index_to_move];
                        }
                        data.pop_back();
                    }
                    return ret;
                }
            };

            // Function to transform a bounding box by a world matrix
            DirectX::BoundingOrientedBox TransformBoundingBox(const DirectX::BoundingOrientedBox& originalBox, const DirectX::XMMATRIX& worldMatrix);

            bool FileExists(const std::string& filename);

            std::string WStringToString(const std::wstring& wstr);
		}
	}
}