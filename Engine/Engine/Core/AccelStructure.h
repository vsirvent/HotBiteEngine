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

#include <Defines.h>
#include <Core/GfxDevice.h>
#include <unordered_map>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace Core {

			//The hardware ray tracing side of the shadow rays: the acceleration structures a compute shader asks with an inline
			//ray query (RayQuery, shader model 6.5) whether something is in the way of a ray, in place of walking the software
			//hierarchy of Core::ShadowRays (which stays: it runs where the device has no ray queries, and it is what the hardware
			//path is compared against).
			//
			//  bottom level   one per range of the shared vertex / index buffers a mesh (or its stand-in for the rays) occupies,
			//                 made the first time an instance needs it and kept: it is the triangles in the mesh's own space, which
			//                 do not change while the mesh does not. The buffers are read where they are (position first, 96 bytes
			//                 a vertex; the indices are relative to the mesh's first vertex), nothing is copied.
			//  top level      one a frame, made of the instances the frame hands to Build (a mesh's range and where it stands). It
			//                 is rebuilt, not updated: a few thousand instances cost the GPU a fraction of a millisecond. One is kept
			//                 for each frame the GPU may still be reading.
			//
			//The builds are recorded into the frame's command list when Build is called, before the shaders that read them. The
			//shader sees the top level as `RaytracingAccelerationStructure` (an SRV: GfxSRV, bound by name like any other).
			class AccelStructure {
			public:
				//One thing the rays can hit: a mesh's triangles, as the range they occupy in the shared buffers, and its world matrix
				//(Transform::world_matrix, the column vector form: the translation is in the last column).
				struct Instance {
					uint32_t vertex_offset = 0;
					uint32_t vertex_count = 0;
					uint32_t index_offset = 0;
					uint32_t index_count = 0;
					float4x4 world = {};
				};

				//Most instances in the top level; Build keeps the first ones past it.
				static constexpr uint32_t MAX_INSTANCES = 16384;
				//The scratch memory the bottom levels made in one frame may use: past it the rest wait for the next frame (their
				//instances are left out of this one's top level).
				static constexpr UINT64 SCRATCH_BUDGET = 128ull * 1024 * 1024;

				struct Stats {
					uint32_t instances = 0;			//in the last top level
					uint32_t skipped = 0;			//handed in and left out (bad ranges, a bottom level still to build)
					uint32_t geometries = 0;		//bottom levels that exist
					uint32_t built = 0;				//bottom levels the last Build made
					uint64_t geometry_bytes = 0;	//all bottom levels
					uint64_t top_bytes = 0;			//the top level of the last Build
					uint64_t scratch_bytes = 0;
					double cpu_ms = 0.0;			//CPU time of the last Build (recording, not the GPU's work)
				};

				AccelStructure() = default;
				~AccelStructure();
				AccelStructure(const AccelStructure&) = delete;
				AccelStructure& operator=(const AccelStructure&) = delete;

				//False when the device has no ray queries: Build then does nothing.
				bool Init();
				void Release();
				bool Supported() const { return supported; }

				//Makes the bottom levels the instances need (those that do not exist yet) and the top level over them, recorded
				//into the open command list. `vertex` and `index` are the shared buffers the instances' ranges are in. False when
				//nothing was built (no instance, or not supported).
				bool Build(GfxBuffer* vertex, GfxBuffer* index, const Instance* instances, uint32_t count);

				//Whether the last Build left a top level to trace.
				bool Ready() const { return ready; }
				GfxSRV* const* TlasSRV() const { return &current_srv; }
				uint32_t InstanceCount() const { return stats.instances; }
				const Stats& GetStats() const { return stats; }
				//Forgets every bottom level (the geometry they were made from is gone).
				void Reset();

			private:
				struct Key {
					uint32_t vertex_offset, vertex_count, index_offset, index_count;
					bool operator==(const Key& o) const {
						return vertex_offset == o.vertex_offset && vertex_count == o.vertex_count &&
							index_offset == o.index_offset && index_count == o.index_count;
					}
				};
				struct KeyHash {
					size_t operator()(const Key& k) const {
						uint64_t h = 1469598103934665603ull;
						for (uint32_t x : { k.vertex_offset, k.vertex_count, k.index_offset, k.index_count }) {
							h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
						}
						return (size_t)h;
					}
				};
				struct Bottom {
					GfxBuffer* buffer = nullptr;
					D3D12_GPU_VIRTUAL_ADDRESS address = 0;
					UINT64 scratch_size = 0;
					bool built = false;
					bool queued = false;
					bool failed = false;
				};
				struct Top {
					GfxBuffer* buffer = nullptr;
					GfxSRV* srv = nullptr;
					uint32_t capacity = 0;
					UINT64 scratch_size = 0;
					//The frame whose commands last built it: free again when the GPU has finished that frame.
					uint64_t serial = 0;
				};
				struct Pending {
					const Key* key;
					Bottom* bottom;
				};

				bool supported = false;
				bool ready = false;
				std::unordered_map<Key, Bottom, KeyHash> bottoms;
				std::vector<Top> tops;
				GfxSRV* current_srv = nullptr;
				GfxBuffer* scratch = nullptr;
				Stats stats;
				//Per call, kept to spare the allocations.
				std::vector<Pending> pending;
				std::vector<const Bottom*> resolved;
				std::vector<uint32_t> resolved_instance;

				void MakeGeometry(const Key& key, GfxBuffer* vertex, GfxBuffer* index, D3D12_RAYTRACING_GEOMETRY_DESC& out) const;
				bool CreateBottom(const Key& key, GfxBuffer* vertex, GfxBuffer* index, Bottom& bottom);
				Top* AcquireTop(uint32_t count);
				bool EnsureScratch(UINT64 bytes);
				void ReleaseTop(Top& top);
			};
		}
	}
}
