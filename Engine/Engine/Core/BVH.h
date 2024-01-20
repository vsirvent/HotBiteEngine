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
#include "Vertex.h"

namespace HotBite {
    namespace Engine {

        int compare_float3(const float3& v1, const float3& v2)
        {
            if (v1.x < v2.x)
                return -1;
            else if (v1.x > v2.x)
                return 1;

            if (v1.y < v2.y)
                return -1;
            else if (v1.y > v2.y)
                return 1;

            if (v1.z < v2.z)
                return -1;
            else if (v1.z > v2.z)
                return 1;

            return 0;
        }

        bool operator>(const float3& v1, const float3& v2)
        {
            return compare_float3(v1, v2) > 0;
        }

        bool operator<(const float3& v1, const float3& v2)
        {
            return compare_float3(v1, v2) < 0;
        }

        const float& component(const float3& v, int index)
        {
            if (index == 0)
                return v.x;
            else if (index == 1)
                return v.y;
            else if (index == 2)
                return v.z;
            else
            {
                throw std::exception("out of index");
            }
        }

        float3 operator+(const float3& v1, const float3& v2)
        {
            return float3{ v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
        }

        float3 operator/(const float3& v1, const float& v2)
        {
            return float3{ v1.x / v2, v1.y / v2, v1.z / v2 };
        }

        namespace Core {
            class BVH {
            public:
                struct Node
                {
                    //--
                    float3 aabb_min{ FLT_MIN, FLT_MIN, FLT_MIN };
                    uint8_t left_child = 0;
                    //--
                    float3 aabb_max{ FLT_MAX, FLT_MAX, FLT_MAX };
                    uint8_t right_child = 0;
                    //--
                    uint32_t index_offset = 0;
                    uint32_t index_count = 0;
                };

                ~BVH() {
                    if (nodes != nullptr) {
                        delete[] nodes;
                    }
                }

                void UpdateNodeBounds(uint32_t node_idx, const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices)
                {
                    Node& node = nodes[node_idx];
                    for (uint32_t i = node.index_offset; i < node.index_count; i += 3)
                    {
                        node.aabb_min = node.aabb_min < vertices[indices[i]].Position? node.aabb_min : vertices[indices[i]].Position;
                        node.aabb_min = node.aabb_min < vertices[indices[i + 1]].Position ? node.aabb_min : vertices[indices[i + 1]].Position;
                        node.aabb_min = node.aabb_min < vertices[indices[i + 2]].Position ? node.aabb_min : vertices[indices[i + 2]].Position;

                        node.aabb_max = node.aabb_max > vertices[indices[i]].Position ? node.aabb_max : vertices[indices[i]].Position;
                        node.aabb_max = node.aabb_max > vertices[indices[i + 1]].Position ? node.aabb_max : vertices[indices[i + 1]].Position;
                        node.aabb_max = node.aabb_max > vertices[indices[i + 2]].Position ? node.aabb_max : vertices[indices[i + 2]].Position;
                    }
                }

                void Subdivide(uint32_t node_idx, const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices, const std::vector<float3>& centroids)
                {
                    Node& node = nodes[node_idx];

                    // terminate recursion
                    if (node.index_count <= 2) return;

                    // determine split axis and position
                    float3 extent = node.aabb_max - node.aabb_min;
                    int axis = 0;
                    if (extent.y > extent.x) axis = 1;
                    if (extent.z > component(extent,axis)) axis = 2;

                    float split_pos = component(node.aabb_min, axis) + component(extent, axis) * 0.5f;

                    // in-place partition
                    int i = node.index_offset;
                    int j = i + node.index_count - 1;
                    while (i <= j)
                    {
                        const auto& centroid = centroids[i];
                        if (component(centroid, axis) < split_pos) {
                            i++;
                        }
                        else {
                            //swap(triIdx[i], triIdx[j--]);
                        }
                    }

                    // abort split if one of the sides is empty
                    int left_count = i - node.index_offset;
                    if (left_count == 0 || left_count == node.index_count) { return; }

                    // create child nodes
                    int left_child_idx = nodes_used++;
                    int right_child_idx = nodes_used++;
    
                    nodes[left_child_idx].index_offset = node.index_offset;
                    nodes[left_child_idx].index_count = left_count;
                    nodes[right_child_idx].index_offset = i;
                    nodes[right_child_idx].index_count = node.index_count - left_count;

                    node.left_child = left_child_idx;
                    node.index_count = 0;

                    UpdateNodeBounds(left_child_idx, vertices, indices);
                    UpdateNodeBounds(right_child_idx, vertices, indices);
                    // recurse
                    Subdivide(left_child_idx, vertices, indices, centroids);
                    Subdivide(right_child_idx, vertices, indices, centroids);
                }

            private:
                Node* nodes = nullptr;
                uint32_t root_idx = 0;
                uint32_t nodes_used = 1;

                void Init(const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices) {
                    if (nodes != nullptr) {
                        delete[] nodes;
                    }
                    nodes = new Node[indices.size() * 2 - 1];
                    root_idx = 0;
                    nodes_used = 1;
                    std::vector<float3> centroids(indices.size() / 3);
                    for (int i = 0; i < indices.size(); i += 3) {
                        centroids[i / 3] = (vertices[indices[i]].Position + vertices[indices[i + 1]].Position + vertices[indices[i + 2]].Position) / 3.0f;
                    }
                    // assign all triangles to root node
                    Node& root = nodes[root_idx];
                    root.index_count = (int32_t)indices.size();
                    UpdateNodeBounds(root_idx, vertices, indices);
                    Subdivide(root_idx, vertices, indices, centroids);
                }

                
            };

        }
    }
}