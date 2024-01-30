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

#include "BVH.h"

namespace HotBite {
	namespace Engine {
		namespace Core {
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


			BVH::BVH() {
			}

			BVH::~BVH() {
				if (nodes != nullptr) {
					delete[] nodes;
				}
			}

			void BVH::Init(const std::vector<HotBite::Engine::Core::Vertex>& vertices, const std::vector<uint32_t>& vertex_idxs) {
				if (nodes != nullptr) {
					delete[] nodes;
					delete[] nodes_idxs;
				}
				
				std::vector<uint32_t> indices(vertex_idxs.size() / 3);
				std::vector<float3> centroids(indices.size());

				nodes = new Node[indices.size() * 2 - 1];
				nodes_idxs = new NodeIdx[indices.size() * 2 - 1];

				//store only first index of the triangle
				for (int i = 0; i < indices.size(); ++i) {
					uint32_t vidx = i * 3;
					indices[i] = vidx;
					centroids[i] = (vertices[vertex_idxs[vidx]].Position + vertices[vertex_idxs[vidx + 1]].Position + vertices[vertex_idxs[vidx + 2]].Position) / 3.0f;
				}

				root_idx = 0;
				nodes_used = 1;

				// assign all triangles to root node
				Node& root = nodes[root_idx];
				NodeIdx& ridx = nodes_idxs[root_idx];
				ridx.index_offset = 0;
				ridx.index_count = (int32_t)indices.size();

				UpdateNodeBounds(root_idx, vertices, indices, vertex_idxs);
				Subdivide(root_idx, vertices, indices, centroids, vertex_idxs);
			}

			void BVH::UpdateNodeBounds(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices,
				                       const std::vector<uint32_t>& triangle_indices, const std::vector<uint32_t>& vertex_idxs)
			{
				Node& node = nodes[node_idx];
				NodeIdx& nidx = nodes_idxs[node_idx];
				for (uint32_t i = nidx.index_offset; i < nidx.index_offset + nidx.index_count; ++i)
				{
					int triangle_idx = triangle_indices[i];
					const auto& v0 = vertices[vertex_idxs[triangle_idx]];
					const auto& v1 = vertices[vertex_idxs[triangle_idx + 1]];
					const auto& v2 = vertices[vertex_idxs[triangle_idx + 2]];

					auto checkMin = [](const float3& p0, const float3& p1) {
						return float3{ fminf(p0.x, p1.x), fminf(p0.y, p1.y), fminf(p0.z, p1.z) };
					};
					
					auto checkMax = [](const float3& p0, const float3& p1) {
						return float3{ fmaxf(p0.x, p1.x), fmaxf(p0.y, p1.y), fmaxf(p0.z, p1.z) };
					};

					node.aabb_min = checkMin(node.aabb_min, v0.Position);
					node.aabb_min = checkMin(node.aabb_min, v1.Position);
					node.aabb_min = checkMin(node.aabb_min, v2.Position);

					node.aabb_max = checkMax(node.aabb_max, v0.Position);
					node.aabb_max = checkMax(node.aabb_max, v1.Position);
					node.aabb_max = checkMax(node.aabb_max, v2.Position);
				}
			}

			void BVH::Subdivide(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices,
				                std::vector<uint32_t>& triangle_indices, std::vector<float3>& centroids, const std::vector<uint32_t>& vertex_idxs)
			{
				Node& node = nodes[node_idx];
				NodeIdx& nidx = nodes_idxs[node_idx];

				// terminate recursion
				if (nidx.index_count < 2) {
					node.index = triangle_indices[nidx.index_offset];
					return;
				}

				// determine split axis and position
				float3 extent = node.aabb_max - node.aabb_min;
				int axis = 0;
				if (extent.y > extent.x) axis = 1;
				if (extent.z > component(extent, axis)) axis = 2;

				float split_pos = component(node.aabb_min, axis) + component(extent, axis) * 0.5f;
#if 1
				// in-place partition
				int i = nidx.index_offset;
				int j = i + nidx.index_count - 1;

				while (i <= j)
				{
					const auto& centroid = centroids[i];
					if (component(centroid, axis) < split_pos) {
						i++;
					}
					else {
						uint32_t tmp = triangle_indices[i];
						triangle_indices[i] = triangle_indices[j];
						triangle_indices[j] = tmp;
						float3 tmp2 = centroids[i];
						centroids[i] = centroids[j];
						centroids[j] = tmp2;
						--j;
					}
				}

				// abort split if one of the sides is empty
				int left_count = i - nidx.index_offset;
				int left_offset = nidx.index_offset;

				int right_count = nidx.index_count - left_count;
				int right_offset = left_count;
#else
				int left_count = 0;
				int right_count = 0;
				std::list<uint32_t> split_triangle_indices;
				std::list<float3> split_centroids;

				for (size_t i = nidx.index_offset; i < nidx.index_offset + nidx.index_count; ++i) {
					const auto& idx = triangle_indices[i];
					const auto& centroid = centroids[i];
					if (component(centroid, axis) < split_pos) {
						split_triangle_indices.push_front(idx);
						split_centroids.push_front(centroid);
						left_count++;
					}
					else {
						split_triangle_indices.push_back(idx);
						split_centroids.push_back(centroid);
						right_count++;
					}
				}
				int n = 0;
				for (const auto& idx : split_triangle_indices) {
					triangle_indices[n++] = idx;
				}
				n = 0;
				for (const auto& idx : split_centroids) {
					centroids[n++] = idx;
				}
				int right_offset = left_count;
				int left_offset = nidx.index_offset;
#endif				
				assert(left_count != 0 || right_count != 0);
				
				if (left_count == 0) {
					left_count++;
					right_offset++;
					right_count--;
				}else if (right_count == 0) {
					right_count++;
					right_offset--;
					left_count--;
				}
				// create child nodes
				int left_child_idx = nodes_used++;
				int right_child_idx = nodes_used++;

				nodes_idxs[left_child_idx].index_offset = left_offset;
				nodes_idxs[left_child_idx].index_count = left_count;
				node.left_child = left_child_idx;

				nodes_idxs[right_child_idx].index_offset = right_offset;
				nodes_idxs[right_child_idx].index_count = right_count;
				node.right_child = right_child_idx;

				UpdateNodeBounds(left_child_idx, vertices, triangle_indices, vertex_idxs);
				UpdateNodeBounds(right_child_idx, vertices, triangle_indices, vertex_idxs);

				Subdivide(left_child_idx, vertices, triangle_indices, centroids, vertex_idxs);
				Subdivide(right_child_idx, vertices, triangle_indices, centroids, vertex_idxs);
				nidx.index_count = 0;

			}
		}
	}
}