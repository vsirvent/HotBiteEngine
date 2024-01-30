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
#include <d3d11.h>
#include <vector>
#include "DXCore.h"

namespace HotBite {
	namespace Engine {
		namespace Core {
			struct Vertex
			{
				float3 Position = {};
				float3 Normal = {};
				float2 UV = {};
				float2 MeshUV = {};
				float3 Tangent = {};
				float3 Bitangent = {};
				int    Boneids[4] = { -1, -1, -1, -1 };
				float4 Weights = { -1.0f, -1.0f, -1.0f, -1.0f };
			};

			struct ParticleVertex
			{
				float3 Position = {};
				float3 Normal = { 0.0f, 1.0f, 0.0f };
				float  Life = 1.0f;
				float  Size = 1.0f;
				uint32_t Id = 0;
				int    Boneids[4] = { -1, -1, -1, -1 };
				float4 Weights = { -1.0f, -1.0f, -1.0f, -1.0f };
				ParticleVertex() = default;
				ParticleVertex(const float3& pos, const float3& normal, float  life,
					float  size, uint32_t id, int bones[4], const float4& weights):
				Position(pos), Normal(normal), Life(life), Size(size), Id(id), Weights(weights) {
					memcpy(Boneids, bones, sizeof(int) * 4);
				}
			};

			template<class T>
			class VertexBuffer {
			private:
				std::vector<uint32_t> vindex;
				std::vector<T> vvertex;
				
				ID3D11Buffer* vertex_buffer = nullptr;
				ID3D11Buffer* index_buffer = nullptr;
				ID3D11ShaderResourceView* vertex_srv = nullptr;
				ID3D11ShaderResourceView* index_srv = nullptr;

			public:
				VertexBuffer() {
				}

				~VertexBuffer() {
					Unprepare();
				}

				size_t GetVertexCount() {
					return vvertex.size();
				}

				size_t GetIndicesCount() {
					return vindex.size();
				}

				void AddMesh(const std::vector<Vertex>& vertices,
					const std::vector<uint32_t>& indices,
					size_t* vertex_offset, size_t* index_offset) {
					*vertex_offset = GetVertexCount();
					*index_offset = GetIndicesCount();
					vvertex.insert(vvertex.end(), vertices.begin(), vertices.end());
					vindex.insert(vindex.end(), indices.begin(), indices.end());
				}

				void FlushMesh(const std::vector<T>& vertices, const std::vector<uint32_t>& indices) {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					D3D11_MAPPED_SUBRESOURCE resource;
					//Update vertex buffer
					context->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
					memcpy(resource.pData, vertices.data(), (UINT)(sizeof(T) * vertices.size()));
					context->Unmap(vertex_buffer, 0);
					//Update index buffer
					context->Map(index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
					memcpy(resource.pData, indices.data(), (UINT)(sizeof(uint32_t) * indices.size()));
					context->Unmap(index_buffer, 0);
				}

				HRESULT Reserve(int size) {
					vvertex.resize(size);
					vindex.resize(size);
					return Prepare(false);
				}

				HRESULT Prepare(bool read_only = true) {
					HRESULT hr = S_OK;
					if (!vvertex.empty())
					{
						ID3D11Device* device = Core::DXCore::Get()->device;
						D3D11_BUFFER_DESC vbd;

						vbd.Usage = read_only ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
						vbd.ByteWidth = (UINT)(sizeof(T) * GetVertexCount());
						vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
						vbd.CPUAccessFlags = read_only ? 0 : D3D11_CPU_ACCESS_WRITE;
						vbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
						vbd.StructureByteStride = sizeof(T);
						D3D11_SUBRESOURCE_DATA initialVertexData;
						initialVertexData.pSysMem = vvertex.data();

						hr = device->CreateBuffer(&vbd, &initialVertexData, &vertex_buffer);
						if (FAILED(hr)) { goto end; }

						D3D11_BUFFER_DESC ibd;
						ibd.Usage = read_only ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
						ibd.ByteWidth = (UINT)(sizeof(uint32_t) * GetIndicesCount());
						ibd.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
						ibd.CPUAccessFlags = read_only ? 0 : D3D11_CPU_ACCESS_WRITE;
						ibd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
						ibd.StructureByteStride = sizeof(uint32_t);
						D3D11_SUBRESOURCE_DATA initialIndexData;
						initialIndexData.pSysMem = vindex.data();

						hr = device->CreateBuffer(&ibd, &initialIndexData, &index_buffer);
						if (FAILED(hr)) { goto end; }

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
						srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
						srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
						srvDesc.BufferEx.FirstElement = 0;
						srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
						srvDesc.BufferEx.NumElements = (uint32_t)GetVertexCount() * sizeof(T) / 4; //sizeof(DXGI_FORMAT_R32_TYPELESS)
						hr = device->CreateShaderResourceView(vertex_buffer, &srvDesc, &vertex_srv);
						if (FAILED(hr)) { goto end; }
						srvDesc.BufferEx.NumElements = (uint32_t)GetIndicesCount();
						hr = device->CreateShaderResourceView(index_buffer, &srvDesc, &index_srv);
						if (FAILED(hr)) { goto end; }
					}
				end:
					return hr;
				}

				void Clean() {
					vindex.clear();
					vvertex.clear();
				}

				void Refresh() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					D3D11_MAPPED_SUBRESOURCE resource;
					//Update vertex buffer
					context->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
					memcpy(resource.pData, vvertex.data(), (UINT)(sizeof(T) * GetVertexCount()));
					context->Unmap(vertex_buffer, 0);
					//Update index buffer
					context->Map(index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
					memcpy(resource.pData, vindex.data(), (UINT)(sizeof(uint32_t) * GetIndicesCount()));
					context->Unmap(index_buffer, 0);
				}

				void SetBuffers() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					uint32_t stride = sizeof(T);
					uint32_t offset = 0;

					context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
					context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);
				}

				HRESULT Unprepare() {
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (vertex_buffer != nullptr) {
						uint32_t stride[1] = {};
						uint32_t offset[1] = {};
						ID3D11Buffer* empty[1] = { nullptr };
						context->IASetVertexBuffers(0, 1, empty, stride, offset);
						int count = vertex_buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						vertex_buffer = nullptr;
						vertex_srv->Release();
						vertex_srv = nullptr;
					}
					if (index_buffer != nullptr) {
						context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
						int count = index_buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						index_buffer = nullptr;
						index_srv->Release();
						index_srv = nullptr;
					}
					return hr;
				}

				ID3D11ShaderResourceView* const* VertexSRV() const {
					return &vertex_srv;
				}

				ID3D11ShaderResourceView* const* IndexSRV() const {
					return &index_srv;
				}
			};
		}
	}
}