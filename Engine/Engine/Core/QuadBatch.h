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

#include "DXCore.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			//Draws one quad at a time from four vertices: what the GUI widgets used DirectXTK's PrimitiveBatch for
			//(Begin / DrawQuad / End), without the batching, which a widget never used. The two triangles are
			//(0,1,2) and (0,2,3), like PrimitiveBatch::DrawQuad. The vertices go through the frame's upload ring;
			//the six indices are shared by every batch of every vertex type.
			template<class V>
			class QuadBatch {
			public:
				QuadBatch() {
					DXCore::Get()->device->CreateBuffer(GfxBufferKind::Dynamic, sizeof(V) * 4, sizeof(V),
						D3D12_RESOURCE_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_GENERIC_READ, &vertices);
				}

				~QuadBatch() {
					if (vertices != nullptr) {
						vertices->Release();
					}
				}

				QuadBatch(const QuadBatch&) = delete;
				QuadBatch& operator=(const QuadBatch&) = delete;

				void Begin() {}
				void End() {}

				void DrawQuad(const V& v0, const V& v1, const V& v2, const V& v3) {
					GfxContext* context = DXCore::Get()->context;
					GfxBuffer* indices = SharedIndices();
					if (vertices == nullptr || indices == nullptr) {
						return;
					}
					const V quad[4] = { v0, v1, v2, v3 };
					context->UpdateBuffer(vertices, quad, sizeof(quad));
					const UINT stride = sizeof(V);
					const UINT offset = 0;
					context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
					context->IASetIndexBuffer(indices, DXGI_FORMAT_R16_UINT, 0);
					context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context->DrawIndexed(6, 0, 0);
				}

			private:
				GfxBuffer* vertices = nullptr;

				//Created on first use and kept for the life of the process (the device outlives every user).
				static GfxBuffer* SharedIndices() {
					static GfxBuffer* indices = nullptr;
					static GfxDevice* owner = nullptr;
					GfxDevice* device = DXCore::Get()->device;
					if (indices == nullptr || owner != device) {
						static const uint16_t data[6] = { 0, 1, 2, 0, 2, 3 };
						indices = nullptr;
						device->CreateBuffer(GfxBufferKind::Static, sizeof(data), sizeof(uint16_t), D3D12_RESOURCE_FLAG_NONE,
							data, GFX_STATE_BUFFER_READ, &indices);
						owner = device;
					}
					return indices;
				}
			};
		}
	}
}
