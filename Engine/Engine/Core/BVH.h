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
				ID3D11Buffer* buffer = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;
				ID3D11Resource* resource = nullptr;

			public:
				DataBuffer() {
				}

				~DataBuffer() {
					Unprepare();
				}

				size_t Size() {
					return size;
				}

				HRESULT Prepare(int size) {
					HRESULT hr = S_OK;
					ID3D11Device* device = Core::DXCore::Get()->device;
					D3D11_BUFFER_DESC bd;
					D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
					D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;

					int32_t data_size = size * sizeof(T);

					bd.Usage = D3D11_USAGE_DEFAULT;
					bd.ByteWidth = data_size;
					bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
					bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
					bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
					bd.StructureByteStride = sizeof(T);
					
					
					D3D11_SUBRESOURCE_DATA initialData{};
					uint8_t* nullData = new uint8_t[data_size];
					memset(nullData, 0, data_size);
					initialData.pSysMem = nullData;
					hr = device->CreateBuffer(&bd, &initialData, &buffer);
					delete[] nullData;
					if (FAILED(hr)) { goto end; }


					srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
					srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
					srvDesc.Buffer.FirstElement = 0;
					srvDesc.Buffer.NumElements = size;
					hr = device->CreateShaderResourceView(buffer, &srvDesc, &srv);
					
					srv->GetResource(&resource);

					uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
					uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
					uav_desc.Buffer.FirstElement = 0;
					uav_desc.Buffer.NumElements = size;
					uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
					hr = device->CreateUnorderedAccessView(resource, &uav_desc, &uav);
					this->size = size;

				end:
					return hr;
				}

				void Clear(T val, uint32_t start = 0, uint32_t len = 0) {
					if (len == 0) {
						len = (uint32_t)size;
					}
					if (len > 0 && start < (uint32_t)size) {
						ID3D11DeviceContext* context = Core::DXCore::Get()->context;
						D3D11_MAPPED_SUBRESOURCE resource;
						context->Map(buffer, 0, D3D11_MAP_WRITE, 0, &resource);
						T* data = (T*)resource.pData;
						for (uint32_t i = start; i < len; ++i) {
							data[i] = val;
						}
						context->Unmap(buffer, 0);
					}
				}


				HRESULT Unprepare() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
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

				ID3D11ShaderResourceView* SRV() const {
					return srv;
				}

				ID3D11UnorderedAccessView*const* UAV() const {
					return &uav;
				}
			};


			template<typename T>
			class Buffer {
			private:
				std::vector<T> nodes;
				ID3D11Buffer* buffer = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;

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
						ID3D11Device* device = Core::DXCore::Get()->device;
						D3D11_BUFFER_DESC vbd;

						vbd.Usage = read_only ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
						vbd.ByteWidth = (UINT)(sizeof(T) * nodes.size());
						vbd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
						vbd.CPUAccessFlags = read_only ? 0 : D3D11_CPU_ACCESS_WRITE;
						vbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
						vbd.StructureByteStride = sizeof(T);
						D3D11_SUBRESOURCE_DATA initialVertexData;
						initialVertexData.pSysMem = nodes.data();

						hr = device->CreateBuffer(&vbd, &initialVertexData, &buffer);
						if (FAILED(hr)) { goto end; }

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
						srvDesc.Format = DXGI_FORMAT_UNKNOWN;
						srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
						srvDesc.Buffer.FirstElement = 0;
						srvDesc.Buffer.NumElements = (uint32_t)nodes.size();
						hr = device->CreateShaderResourceView(buffer, &srvDesc, &srv);
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
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					D3D11_MAPPED_SUBRESOURCE resource;
					//Update vertex buffer
					context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
					memcpy(resource.pData, nodes.data(), (UINT)(sizeof(BVHNode) * len));
					context->Unmap(buffer, 0);
				}

				HRESULT Unprepare() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (buffer != nullptr) {
						int count = buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						srv->Release();
						srv = nullptr;
					}
					return hr;
				}

				ID3D11ShaderResourceView* const* SRV() const {
					return &srv;
				}
			};

			template<typename T>
			class ExtBuffer {
			private:
				ID3D11Buffer* buffer = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;

			public:
				ExtBuffer() {
				}

				~ExtBuffer() {
					Unprepare();
				}

				HRESULT Prepare(uint32_t size) {
					HRESULT hr = S_OK;
					ID3D11Device* device = Core::DXCore::Get()->device;
					D3D11_BUFFER_DESC vbd;
					D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

					vbd.Usage = D3D11_USAGE_DYNAMIC;
					vbd.ByteWidth = (UINT)(sizeof(T) * size);
					vbd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
					vbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
					vbd.StructureByteStride = sizeof(T);
					
					hr = device->CreateBuffer(&vbd, nullptr, &buffer);
					if (FAILED(hr)) { goto end; }

					srvDesc.Format = DXGI_FORMAT_UNKNOWN;
					srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
					srvDesc.Buffer.FirstElement = 0;
					srvDesc.Buffer.NumElements = size;
					hr = device->CreateShaderResourceView(buffer, &srvDesc, &srv);
					if (FAILED(hr)) { goto end; }
				end:
					return hr;
				}

				void Refresh(const T* data, uint32_t start, uint32_t len) {
					if (len > 0) {
						ID3D11DeviceContext* context = Core::DXCore::Get()->context;
						D3D11_MAPPED_SUBRESOURCE resource;
						//Update vertex buffer
						context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
						memcpy(resource.pData, data, (UINT)(sizeof(T) * len));
						context->Unmap(buffer, 0);
					}
				}

				HRESULT Unprepare() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (buffer != nullptr) {
						int count = buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						srv->Release();
						srv = nullptr;
					}
					return hr;
				}

				ID3D11ShaderResourceView* const* SRV() const {
					return &srv;
				}
			};

			using BVHBuffer = Buffer<BVHNode>;
			using ExtBVHBuffer = ExtBuffer<BVHNode>;
		}
	}
}