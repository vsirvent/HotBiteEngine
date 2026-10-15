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

#include <cstdint>
#include "DXCore.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			//A GPU-writable raw buffer: `RWByteAddressBuffer` in HLSL. One of the three
			//GPU-written buffer types, alongside RWStructuredBuffer and RWTypedBuffer
			//below; Core::Buffer and Core::ExtBuffer are upload-only, SRV-only, because
			//until the radiance cache nothing needed a shader to build state at all.
			//
			//Raw rather than structured on purpose. SM5 guarantees the interlocked
			//operations on a RWByteAddressBuffer; on a RWStructuredBuffer they are
			//only defined for a buffer whose *element* is a scalar uint, so a struct
			//with a uint field is a driver-dependent bet. The radiance cache needs
			//InterlockedCompareExchange on the key and InterlockedAdd on the
			//accumulator of the same 32-byte record, so it is raw and the offsets are
			//spelled out in RadianceCache.hlsli.
			//
			//Bound as a UAV *and* an SRV: a pass that only reads should take the SRV
			//so it does not serialize against other UAV users, but the ray tracers are
			//already at the cs_5_0 texture register limit (see GIRayTraceCS.hlsl), and
			//a Buffer SRV costs a `t` register where a UAV costs a `u` register - of
			//which they use three of the available eight. So the tracers read the
			//cache through the UAV, deliberately, and only the mixer's debug view
			//takes the SRV.
			class RWByteBuffer {
			private:
				GfxBuffer* buffer = nullptr;
				GfxUAV* uav = nullptr;
				GfxSRV* srv = nullptr;

				//Readback ring. Reading the buffer written this frame would block
				//until the GPU drained, so the CPU reads a copy taken several frames
				//ago and never waits: these are counters for a readout, and a stale
				//value is worth vastly more than a pipeline stall every frame.
				//
				//Eight deep, and that depth is the whole trick: with the CPU recording
				//frames ahead of the GPU a copy is only readable some frames after it was
				//made, and a ring that is too shallow never has one ready, so the
				//counters froze at their first reading and looked exactly like a cache that
				//had stopped filling. Seven frames of slack, plus the scan below, makes a
				//failure mean the GPU is genuinely stalled rather than merely busy.
				static constexpr uint32_t RING = 8;
				GfxBuffer* staging[RING] = {};
				//Whether a slot holds a copy that was actually issued, and the frame fence
				//after which it can be read. A slot nothing was ever copied into would read
				//back zeros, which would report an empty cache rather than no reading at all.
				bool pending[RING] = {};
				uint64_t ready[RING] = {};
				uint32_t ring_pos = 0;
				uint32_t size_bytes = 0;

			public:
				RWByteBuffer() = default;
				~RWByteBuffer() { Release(); }

				RWByteBuffer(const RWByteBuffer&) = delete;
				RWByteBuffer& operator=(const RWByteBuffer&) = delete;

				uint32_t SizeBytes() const { return size_bytes; }
				bool IsValid() const { return buffer != nullptr; }

				//`bytes` is rounded up to a multiple of 4 - a raw view addresses
				//32-bit words. `readback` allocates the staging ring; pass it only for
				//the small counter buffers, never for the cache itself.
				HRESULT Init(uint32_t bytes, bool readback = false) {
					Release();
					GfxDevice* device = Core::DXCore::Get()->device;
					size_bytes = (bytes + 3u) & ~3u;

					HRESULT hr = device->CreateBuffer(GfxBufferKind::Static, size_bytes, 0,
						D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = DXGI_FORMAT_R32_TYPELESS;
						ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = size_bytes / 4;
						ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
						hr = device->CreateUAV(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = DXGI_FORMAT_R32_TYPELESS;
						sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						sd.Buffer.FirstElement = 0;
						sd.Buffer.NumElements = size_bytes / 4;
						sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
						hr = device->CreateSRV(buffer, &sd, &srv);
						if (FAILED(hr)) { goto end; }
					}

					if (readback) {
						for (uint32_t i = 0; i < RING; ++i) {
							hr = device->CreateBuffer(GfxBufferKind::Readback, size_bytes, 0, D3D12_RESOURCE_FLAG_NONE,
								nullptr, D3D12_RESOURCE_STATE_COPY_DEST, &staging[i]);
							if (FAILED(hr)) { goto end; }
						}
					}
				end:
					return hr;
				}

				void Release() {
					if (uav != nullptr) { uav->Release(); uav = nullptr; }
					if (srv != nullptr) { srv->Release(); srv = nullptr; }
					if (buffer != nullptr) { buffer->Release(); buffer = nullptr; }
					for (uint32_t i = 0; i < RING; ++i) {
						if (staging[i] != nullptr) { staging[i]->Release(); staging[i] = nullptr; }
						pending[i] = false;
					}
					ring_pos = 0;
					size_bytes = 0;
				}

				void Clear(uint32_t value = 0) {
					if (uav == nullptr) { return; }
					const UINT v[4] = { value, value, value, value };
					Core::DXCore::Get()->context->ClearUnorderedAccessViewUint(uav, v);
				}

				//Take this frame's copy and hand back the oldest one that is ready.
				//Returns false (leaving `dst` untouched) when the copy has not landed,
				//which is the normal answer for the first RING-1 calls and whenever the
				//GPU is behind. Callers keep their previous value.
				bool Readback(void* dst, uint32_t bytes) {
					if (buffer == nullptr || staging[0] == nullptr) { return false; }
					GfxContext* context = Core::DXCore::Get()->context;
					GfxDevice* device = Core::DXCore::Get()->device;
					context->CopyResource(staging[ring_pos], buffer);
					pending[ring_pos] = true;
					ready[ring_pos] = context->FrameFence();
					ring_pos = (ring_pos + 1) % RING;

					//Oldest first: that is the copy most likely to have landed, and
					//taking it rather than the newest readable one keeps the reported
					//value's age stable instead of jittering with GPU load.
					for (uint32_t i = 0; i < RING; ++i) {
						const uint32_t slot = (ring_pos + i) % RING;
						if (!pending[slot]) { continue; }
						if (!device->IsFrameComplete(ready[slot])) { continue; }
						memcpy(dst, staging[slot]->mapped, min(bytes, size_bytes));
						pending[slot] = false;
						return true;
					}
					return false;
				}

				GfxUAV* UAV() const { return uav; }
				GfxSRV* SRV() const { return srv; }
				GfxSRV* const* SRVAddr() const { return &srv; }
			};

			//A GPU-written structured buffer: `RWStructuredBuffer<T>` to the pass that
			//fills it, `StructuredBuffer<T>` to the pass that reads it. Both views exist
			//because that hand-off is the only reason this type is here - the splat
			//preprocess writes the projected Gaussians and the rasterizer reads them, and
			//a read-only pass should take the SRV so it does not serialize against the
			//writer.
			//
			//Structured rather than raw (unlike RWByteBuffer above) because neither side
			//needs an interlocked operation on the record: one thread owns one element.
			//The price is that `stride` MUST equal the stride fxc computed for the HLSL
			//struct, and NOTHING CHECKS IT - a mismatch reads neighbouring elements as
			//this one's fields. Get the number from the disassembly
			//(`fxc /dumpbin` -> `dcl_resource_structured tN, <stride>`) rather than from
			//sizeof() on the C++ mirror, and static_assert the mirror against it.
			class RWStructuredBuffer {
			private:
				GfxBuffer* buffer = nullptr;
				GfxUAV* uav = nullptr;
				GfxSRV* srv = nullptr;
				uint32_t stride = 0;
				uint32_t count = 0;

			public:
				RWStructuredBuffer() = default;
				~RWStructuredBuffer() { Release(); }

				RWStructuredBuffer(const RWStructuredBuffer&) = delete;
				RWStructuredBuffer& operator=(const RWStructuredBuffer&) = delete;

				uint32_t Stride() const { return stride; }
				uint32_t Count() const { return count; }
				bool IsValid() const { return buffer != nullptr; }

				HRESULT Init(uint32_t element_stride, uint32_t element_count) {
					Release();
					GfxDevice* device = Core::DXCore::Get()->device;
					stride = element_stride;
					count = element_count;

					HRESULT hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)stride * count, stride,
						D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						//DXGI_FORMAT_UNKNOWN is required for a structured view: the
						//element layout comes from the stride, not from a format.
						D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = DXGI_FORMAT_UNKNOWN;
						ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = count;
						ud.Buffer.StructureByteStride = stride;
						ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
						hr = device->CreateUAV(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = DXGI_FORMAT_UNKNOWN;
						sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						sd.Buffer.FirstElement = 0;
						sd.Buffer.NumElements = count;
						sd.Buffer.StructureByteStride = stride;
						hr = device->CreateSRV(buffer, &sd, &srv);
						if (FAILED(hr)) { goto end; }
					}
				end:
					return hr;
				}

				void Release() {
					if (uav != nullptr) { uav->Release(); uav = nullptr; }
					if (srv != nullptr) { srv->Release(); srv = nullptr; }
					if (buffer != nullptr) { buffer->Release(); buffer = nullptr; }
					stride = 0;
					count = 0;
				}

				GfxUAV* UAV() const { return uav; }
				GfxSRV* SRV() const { return srv; }
			};

			//A GPU-written *typed* buffer: `RWBuffer<uint>` to the writer, `Buffer<uint>`
			//to the reader. The counterpart of RWStructuredBuffer for the case where the
			//element is a scalar the format already describes, which is what makes
			//Clear() possible - ClearUnorderedAccessViewUint needs a typed or raw view
			//and does nothing useful on a structured one.
			class RWTypedBuffer {
			private:
				GfxBuffer* buffer = nullptr;
				GfxUAV* uav = nullptr;
				GfxSRV* srv = nullptr;
				uint32_t count = 0;

			public:
				RWTypedBuffer() = default;
				~RWTypedBuffer() { Release(); }

				RWTypedBuffer(const RWTypedBuffer&) = delete;
				RWTypedBuffer& operator=(const RWTypedBuffer&) = delete;

				uint32_t Count() const { return count; }
				bool IsValid() const { return buffer != nullptr; }

				//`element_bytes` must match `format`; they are separate arguments only
				//because DXGI has no size-of-format query.
				//
				//`indirect_args` is kept for the callers that say what the buffer is for: in D3D12 any
				//buffer can be the argument buffer of DispatchIndirect (GfxContext moves it to the
				//INDIRECT_ARGUMENT state for the call and back for the UAV), which is what lets a shader
				//decide how many thread groups the *next* dispatch launches without a readback - the
				//splat tile compaction builds its group count with an InterlockedAdd straight into
				//element 0.
				HRESULT Init(uint32_t element_count, DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
					uint32_t element_bytes = 4, bool indirect_args = false) {
					(void)indirect_args;
					Release();
					GfxDevice* device = Core::DXCore::Get()->device;
					count = element_count;

					HRESULT hr = device->CreateBuffer(GfxBufferKind::Static, (UINT64)element_bytes * count, 0,
						D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = format;
						ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = count;
						ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
						hr = device->CreateUAV(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = format;
						sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						sd.Buffer.FirstElement = 0;
						sd.Buffer.NumElements = count;
						hr = device->CreateSRV(buffer, &sd, &srv);
						if (FAILED(hr)) { goto end; }
					}
				end:
					return hr;
				}

				void Release() {
					if (uav != nullptr) { uav->Release(); uav = nullptr; }
					if (srv != nullptr) { srv->Release(); srv = nullptr; }
					if (buffer != nullptr) { buffer->Release(); buffer = nullptr; }
					count = 0;
				}

				void Clear(uint32_t value = 0) {
					if (uav == nullptr) { return; }
					const UINT v[4] = { value, value, value, value };
					Core::DXCore::Get()->context->ClearUnorderedAccessViewUint(uav, v);
				}

				GfxUAV* UAV() const { return uav; }
				GfxSRV* SRV() const { return srv; }
				//Only DispatchIndirect wants this; everything else goes through the two views.
				GfxBuffer* Buffer() const { return buffer; }
			};
		}
	}
}
