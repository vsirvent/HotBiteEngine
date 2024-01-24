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
            class BVH {
            public:
                struct Node
                {
                    //--
                    float3 aabb_min{ FLT_MAX, FLT_MAX, FLT_MAX };
                    uint16_t left_child = 0;
                    uint16_t right_child = 0;
                    //--
                    float3 aabb_max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
                    uint32_t index = 0;
                };

                struct NodeIdx
                {
                    uint32_t index_offset = 0;
                    uint32_t index_count = 0;
                };

                BVH();
                ~BVH();
				       
                void Init(const std::vector<HotBite::Engine::Core::Vertex>& vertices, const std::vector<uint32_t>& vertex_idxs);

				const Node* Root() const { return nodes; }
				uint32_t Size() const { return nodes_used; }

            private:
                Node* nodes = nullptr;
                NodeIdx* nodes_idxs = nullptr;
                uint32_t root_idx = 0;
                uint32_t nodes_used = 0;

                void UpdateNodeBounds(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices, const std::vector<uint32_t>& triangle_indices, const std::vector<uint32_t>& vertex_idxs);
                void Subdivide(uint32_t node_idx, const std::vector<HotBite::Engine::Core::Vertex>& vertices, std::vector<uint32_t>& triangle_indices, std::vector<float3>& centroids, const std::vector<uint32_t>& vertex_idxs);
            };

			class BVHBuffer {
			private:
				std::vector<BVH::Node> nodes;
				ID3D11Buffer* buffer = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;

			public:
				BVHBuffer() {
				}

				~BVHBuffer() {
					Unprepare();
				}

				size_t GetBVHCount() {
					return nodes.size();
				}

				void AddBVH(const BVH& bvh, size_t* offset) {
					*offset = nodes.size();
					nodes.reserve(nodes.size() + bvh.Size());
					for (size_t i = 0; i < bvh.Size(); ++i) {
						nodes.push_back(*bvh.Root());
					}					
				}
				
				HRESULT Prepare(bool read_only = true) {
					HRESULT hr = S_OK;
					if (!nodes.empty())
					{
						ID3D11Device* device = Core::DXCore::Get()->device;
						D3D11_BUFFER_DESC vbd;

						vbd.Usage = read_only ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
						vbd.ByteWidth = (UINT)(sizeof(BVH::Node) * nodes.size());
						vbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_SHADER_RESOURCE;
						vbd.CPUAccessFlags = read_only ? 0 : D3D11_CPU_ACCESS_WRITE;
						vbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
						vbd.StructureByteStride = 0;
						D3D11_SUBRESOURCE_DATA initialVertexData;
						initialVertexData.pSysMem = nodes.data();

						hr = device->CreateBuffer(&vbd, &initialVertexData, &buffer);
						if (FAILED(hr)) { goto end; }

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
						srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
						srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
						srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
						srvDesc.BufferEx.NumElements = (uint32_t)nodes.size() * sizeof(BVH::Node) / 4;
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
					memcpy(resource.pData, nodes.data(), (UINT)(sizeof(BVH::Node) * len));
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

				ID3D11ShaderResourceView* SRV() const {
					return srv;
				}
			};
        }
    }
}