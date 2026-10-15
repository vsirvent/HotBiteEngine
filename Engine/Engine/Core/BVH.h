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
		namespace Core {
			struct ObjectInfo
			{
				float4x4 world;
				float4x4 inv_world;

				float3 aabb_min;
				uint32_t object_offset;

				float3 aabb_max;
				uint32_t vertex_offset;

				float3 position;
				uint32_t index_offset;

				float density;
				float opacity;
				float padding0;
				float padding1;
			};


			struct BVHNode
			{
				//--
				float3 aabb_min{ FLT_MAX, FLT_MAX, FLT_MAX };
				uint16_t left_child = 0;
				uint16_t right_child = 0;
				//--
				float3 aabb_max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
				uint32_t index = 0;
			};


			class TBVH {
			private:
				struct NodeIdx
				{
					uint32_t index_offset = 0;
					uint32_t index_count = 0;
				};
			public:

				TBVH(uint32_t max_size);
				~TBVH();

				void Load(const ObjectInfo* objects, int size);

				const BVHNode* Root() const { return nodes; }
				uint32_t Size() const { return nodes_used; }

			private:
				BVHNode* nodes = nullptr;
				NodeIdx* nodes_idxs = nullptr;
				uint32_t* indices = nullptr;
				uint32_t root_idx = 0;
				uint32_t nodes_used = 0;

				void UpdateNodeBounds(uint32_t node_idx, const ObjectInfo* objects, const uint32_t* indices);
				void Subdivide(uint32_t node_idx, const ObjectInfo* objects, uint32_t* indices);
			};

			class BVH {
			public:
				

				struct NodeIdx
				{
					uint32_t index_offset = 0;
					uint32_t index_count = 0;
				};

				BVH();
				~BVH();

				void Init(const std::vector<HotBite::Engine::Core::Vertex>& vertices, const std::vector<uint32_t>& vertex_idxs);

				const BVHNode* Root() const { return nodes; }
				uint32_t Size() const { return nodes_used; }

			private:
				BVHNode* nodes = nullptr;
				NodeIdx* nodes_idxs = nullptr;
				uint32_t root_idx = 0;
				uint32_t nodes_used = 0;

				void UpdateNodeBounds(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices, const std::vector<uint32_t>& triangle_indices, const std::vector<uint32_t>& vertex_idxs);
				void Subdivide(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices, std::vector<uint32_t>& triangle_indices, std::vector<float3>& centroids, const std::vector<uint32_t>& vertex_idxs);
			};

			template<typename T>
			class DataBuffer {
			private:
				int size = 0;
				GfxBuffer* buffer = nullptr;
				GfxSRV* srv = nullptr;
				GfxUAV* uav = nullptr;
				GfxResource* resource = nullptr;

			public:
				DataBuffer() {
				}

				~DataBuffer() {
					Unprepare();
				}

				size_t Size() {
					return size;
				}

				HRESULT Prepare(int size, DXGI_FORMAT format) {
					HRESULT hr = S_OK;
					GfxDevice* device = Core::DXCore::Get()->device;

					int32_t data_size = size * sizeof(T);

					D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
					D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};

					uint8_t* nullData = new uint8_t[data_size];
					memset(nullData, 0, data_size);
					hr = device->CreateBuffer(GfxBufferKind::Static, data_size, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
						nullData, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer);
					delete[] nullData;
					if (FAILED(hr)) { goto end; }

					srvDesc.Format = format;
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srvDesc.Buffer.FirstElement = 0;
					srvDesc.Buffer.NumElements = size;
					hr = device->CreateSRV(buffer, &srvDesc, &srv);
					if (FAILED(hr)) { goto end; }

					uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					uav_desc.Format = format;
					uav_desc.Buffer.FirstElement = 0;
					uav_desc.Buffer.NumElements = size;
					hr = device->CreateUAV(buffer, &uav_desc, &uav);
					this->size = size;

				end:
					return hr;
				}

				void Clear(T val, uint32_t start = 0, uint32_t len = 0) {
					return;
					if (len == 0) {
						len = (uint32_t)size;
					}
					if (len > 0 && start < (uint32_t)size) {
						//(Disabled above, as it always was: nothing clears this buffer from the CPU.)
					(void)val;
					}
				}


				HRESULT Unprepare() {
					GfxContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (buffer != nullptr) {
						int count = buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						if (srv != nullptr) {
							srv->Release();
							srv = nullptr;
						}
						if (uav != nullptr) {
							uav->Release();
							uav = nullptr;
						}
					}
					size = 0;
					return hr;
				}

				GfxSRV* SRV() const {
					return srv;
				}

				GfxUAV*const* UAV() const {
					return &uav;
				}
			};


			template<typename T>
			class Buffer {
			private:
				std::vector<T> nodes;
				GfxBuffer* buffer = nullptr;
				GfxSRV* srv = nullptr;

			public:
				Buffer() {
				}

				~Buffer() {
					Unprepare();
				}

				size_t Size() {
					return nodes.size();
				}

				void Add(const T& element, size_t* offset) {
					*offset = nodes.size();
					nodes.push_back(element);
				}

				void Add(const T* element, uint32_t count, size_t* offset) {
					*offset = nodes.size();
					for (uint32_t i = 0; i < count; ++i) {
						nodes.push_back(element[i]);
					}
				}

				HRESULT Prepare(bool read_only = true) {
					HRESULT hr = S_OK;
					if (!nodes.empty())
					{
						GfxDevice* device = Core::DXCore::Get()->device;
						(void)read_only; // both kinds are GPU memory now; a writer goes through Refresh (a copy)
						hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)sizeof(T) * nodes.size(), sizeof(T),
							D3D12_RESOURCE_FLAG_NONE, nodes.data(), GFX_STATE_BUFFER_READ, &buffer);
						if (FAILED(hr)) { goto end; }

						D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
						srvDesc.Format = DXGI_FORMAT_UNKNOWN;
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.Buffer.FirstElement = 0;
						srvDesc.Buffer.NumElements = (uint32_t)nodes.size();
						srvDesc.Buffer.StructureByteStride = sizeof(T);
						hr = device->CreateSRV(buffer, &srvDesc, &srv);
						if (FAILED(hr)) { goto end; }
					}
				end:
					return hr;
				}

				void Clean() {
					nodes.clear();
				}

				void Refresh(uint32_t start = 0, uint32_t len = 0) {
					if (len == 0) {
						len = (uint32_t)nodes.size() - start;
					}
					GfxContext* context = Core::DXCore::Get()->context;
					context->UpdateBuffer(buffer, nodes.data(), (UINT64)sizeof(BVHNode) * len);
				}

				HRESULT Unprepare() {
					GfxContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (buffer != nullptr) {
						int count = buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						//Null out so repeated Unprepare/Prepare cycles (buffer rebuilds
						//after post-Init mesh loads) can't double-release.
						buffer = nullptr;
						srv->Release();
						srv = nullptr;
					}
					return hr;
				}

				GfxSRV* const* SRV() const {
					return &srv;
				}
			};

			template<typename T>
			class ExtBuffer {
			private:
				GfxBuffer* buffer = nullptr;
				GfxSRV* srv = nullptr;

			public:
				ExtBuffer() {
				}

				~ExtBuffer() {
					Unprepare();
				}

				HRESULT Prepare(uint32_t size) {
					HRESULT hr = S_OK;
					GfxDevice* device = Core::DXCore::Get()->device;
					D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

					hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)sizeof(T) * size, sizeof(T), D3D12_RESOURCE_FLAG_NONE,
						nullptr, GFX_STATE_BUFFER_READ, &buffer);
					if (FAILED(hr)) { goto end; }

					srvDesc.Format = DXGI_FORMAT_UNKNOWN;
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srvDesc.Buffer.FirstElement = 0;
					srvDesc.Buffer.NumElements = size;
					srvDesc.Buffer.StructureByteStride = sizeof(T);
					hr = device->CreateSRV(buffer, &srvDesc, &srv);
					if (FAILED(hr)) { goto end; }
				end:
					return hr;
				}

				void Refresh(const T* data, uint32_t start, uint32_t len) {
					if (len > 0) {
						GfxContext* context = Core::DXCore::Get()->context;
						context->UpdateBuffer(buffer, data, (UINT64)sizeof(T) * len);
					}
				}

				HRESULT Unprepare() {
					GfxContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (buffer != nullptr) {
						int count = buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						//Null both, so an explicit Unprepare followed by the destructor's
						//(or a second Prepare) does not release twice.
						buffer = nullptr;
						if (srv != nullptr) {
							srv->Release();
							srv = nullptr;
						}
					}
					return hr;
				}

				GfxSRV* const* SRV() const {
					return &srv;
				}
			};

			using BVHBuffer = Buffer<BVHNode>;
			using ExtBVHBuffer = ExtBuffer<BVHNode>;
		}
	}
}