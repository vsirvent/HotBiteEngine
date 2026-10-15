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
#include "GfxDevice.h"
#include <algorithm>
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
				
				GfxBuffer* vertex_buffer = nullptr;
				GfxBuffer* index_buffer = nullptr;
				GfxSRV* vertex_srv = nullptr;
				GfxSRV* index_srv = nullptr;

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

				//Overwrites the vertices one mesh contributed, in place. The CPU copy
				//only - the GPU buffer is immutable and is rebuilt from this by
				//Unprepare()+Prepare(), so a caller changing several meshes pays for
				//one rebuild rather than one each (World::FlushMeshBuffers).
				void UpdateMesh(size_t vertex_offset, const std::vector<T>& vertices) {
					if (vertex_offset + vertices.size() > vvertex.size()) {
						return;
					}
					std::copy(vertices.begin(), vertices.end(), vvertex.begin() + vertex_offset);
				}

				void FlushMesh(const std::vector<T>& vertices, const std::vector<uint32_t>& indices) {
					GfxContext* context = Core::DXCore::Get()->context;
					//Update vertex buffer
					context->UpdateBuffer(vertex_buffer, vertices.data(), (UINT64)sizeof(T) * vertices.size());
					//Update index buffer
					context->UpdateBuffer(index_buffer, indices.data(), (UINT64)sizeof(uint32_t) * indices.size());
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
						GfxDevice* device = Core::DXCore::Get()->device;
						//Vertex and index data live in GPU memory in the one state that serves as vertex buffer, index buffer
						//and SRV (the ray tracers read them as raw buffers), so nothing ever has to move them between uses.
						(void)read_only; // an update is a copy (Refresh/FlushMesh), whichever it was called with
						hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)sizeof(T) * GetVertexCount(), sizeof(T),
							D3D12_RESOURCE_FLAG_NONE, vvertex.data(), GFX_STATE_BUFFER_READ, &vertex_buffer);
						if (FAILED(hr)) { goto end; }

						hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)sizeof(uint32_t) * GetIndicesCount(), sizeof(uint32_t),
							D3D12_RESOURCE_FLAG_NONE, vindex.data(), GFX_STATE_BUFFER_READ, &index_buffer);
						if (FAILED(hr)) { goto end; }

						D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
						srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.Buffer.FirstElement = 0;
						srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
						srvDesc.Buffer.NumElements = (uint32_t)GetVertexCount() * sizeof(T) / 4; //sizeof(DXGI_FORMAT_R32_TYPELESS)
						hr = device->CreateSRV(vertex_buffer, &srvDesc, &vertex_srv);
						if (FAILED(hr)) { goto end; }
						srvDesc.Buffer.NumElements = (uint32_t)GetIndicesCount();
						hr = device->CreateSRV(index_buffer, &srvDesc, &index_srv);
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
					GfxContext* context = Core::DXCore::Get()->context;
					//Update vertex buffer
					context->UpdateBuffer(vertex_buffer, vvertex.data(), (UINT64)sizeof(T) * GetVertexCount());
					//Update index buffer
					context->UpdateBuffer(index_buffer, vindex.data(), (UINT64)sizeof(uint32_t) * GetIndicesCount());
				}

				void SetBuffers() {
					GfxContext* context = Core::DXCore::Get()->context;
					uint32_t stride = sizeof(T);
					uint32_t offset = 0;

					context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
					context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);
				}

				HRESULT Unprepare() {
					GfxContext* context = Core::DXCore::Get()->context;
					HRESULT hr = S_OK;
					if (vertex_buffer != nullptr) {
						uint32_t stride[1] = {};
						uint32_t offset[1] = {};
						GfxBuffer* empty[1] = { nullptr };
						context->IASetVertexBuffers(0, 1, empty, stride, offset);
						int count = vertex_buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						vertex_buffer = nullptr;
						//Individually checked, not assumed to rise and fall together with
						//vertex_buffer: only ever mattered once an instance could be
						//Unprepare()'d outside process shutdown (Core::MeshData::InitOwned's
						//per-chunk buffers, torn down on unload) - a Prepare() that created
						//the buffer but failed the SRV would otherwise crash here on the
						//very next unload.
						if (vertex_srv != nullptr) {
							vertex_srv->Release();
							vertex_srv = nullptr;
						}
					}
					if (index_buffer != nullptr) {
						context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
						int count = index_buffer->Release();
						hr |= (count == 0) ? S_OK : E_FAIL;
						index_buffer = nullptr;
						if (index_srv != nullptr) {
							index_srv->Release();
							index_srv = nullptr;
						}
					}
					return hr;
				}

				GfxSRV* const* VertexSRV() const {
					return &vertex_srv;
				}

				GfxSRV* const* IndexSRV() const {
					return &index_srv;
				}
			};
		}
	}
}