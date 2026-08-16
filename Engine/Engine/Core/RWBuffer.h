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

#include <d3d11.h>
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
				ID3D11Buffer* buffer = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;

				//Readback ring. A Map of the buffer written this frame would block
				//until the GPU drained, so the CPU reads a copy taken several frames
				//ago and never waits: these are counters for a readout, and a stale
				//value is worth vastly more than a pipeline stall every frame.
				//
				//Eight deep, and that depth is the whole trick. At three (two frames
				//of slack) the non-blocking Map failed essentially every call once the
				//app was running at speed and the driver was buffering frames ahead -
				//it succeeded once during startup and then never again, so the counters
				//froze at their first reading and looked exactly like a cache that had
				//stopped filling. Seven frames of slack, plus the scan below, makes a
				//failure mean the GPU is genuinely stalled rather than merely busy.
				static constexpr uint32_t RING = 8;
				ID3D11Buffer* staging[RING] = {};
				//Whether a slot holds a copy that was actually issued. Mapping a slot
				//nothing was ever copied into succeeds and returns zeros, which would
				//report an empty cache rather than no reading at all.
				bool pending[RING] = {};
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
					ID3D11Device* device = Core::DXCore::Get()->device;
					size_bytes = (bytes + 3u) & ~3u;

					D3D11_BUFFER_DESC bd = {};
					bd.Usage = D3D11_USAGE_DEFAULT;
					bd.ByteWidth = size_bytes;
					bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
					bd.CPUAccessFlags = 0;
					bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
					bd.StructureByteStride = 0;

					HRESULT hr = device->CreateBuffer(&bd, nullptr, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = DXGI_FORMAT_R32_TYPELESS;
						ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = size_bytes / 4;
						ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
						hr = device->CreateUnorderedAccessView(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = DXGI_FORMAT_R32_TYPELESS;
						sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
						sd.BufferEx.FirstElement = 0;
						sd.BufferEx.NumElements = size_bytes / 4;
						sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
						hr = device->CreateShaderResourceView(buffer, &sd, &srv);
						if (FAILED(hr)) { goto end; }
					}

					if (readback) {
						D3D11_BUFFER_DESC sbd = {};
						sbd.Usage = D3D11_USAGE_STAGING;
						sbd.ByteWidth = size_bytes;
						sbd.BindFlags = 0;
						sbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						sbd.MiscFlags = 0;
						for (uint32_t i = 0; i < RING; ++i) {
							hr = device->CreateBuffer(&sbd, nullptr, &staging[i]);
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
					ID3D11DeviceContext* context = Core::DXCore::Get()->context;
					context->CopyResource(staging[ring_pos], buffer);
					pending[ring_pos] = true;
					ring_pos = (ring_pos + 1) % RING;

					//Oldest first: that is the copy most likely to have landed, and
					//taking it rather than the newest readable one keeps the reported
					//value's age stable instead of jittering with GPU load.
					for (uint32_t i = 0; i < RING; ++i) {
						const uint32_t slot = (ring_pos + i) % RING;
						if (!pending[slot]) { continue; }
						D3D11_MAPPED_SUBRESOURCE mapped = {};
						HRESULT hr = context->Map(staging[slot], 0, D3D11_MAP_READ,
							D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
						if (FAILED(hr)) { continue; }
						memcpy(dst, mapped.pData, min(bytes, size_bytes));
						context->Unmap(staging[slot], 0);
						pending[slot] = false;
						return true;
					}
					return false;
				}

				ID3D11UnorderedAccessView* UAV() const { return uav; }
				ID3D11ShaderResourceView* SRV() const { return srv; }
				ID3D11ShaderResourceView* const* SRVAddr() const { return &srv; }
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
				ID3D11Buffer* buffer = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
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
					ID3D11Device* device = Core::DXCore::Get()->device;
					stride = element_stride;
					count = element_count;

					D3D11_BUFFER_DESC bd = {};
					bd.Usage = D3D11_USAGE_DEFAULT;
					bd.ByteWidth = stride * count;
					bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
					bd.CPUAccessFlags = 0;
					bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
					bd.StructureByteStride = stride;

					HRESULT hr = device->CreateBuffer(&bd, nullptr, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						//DXGI_FORMAT_UNKNOWN is required for a structured view: the
						//element layout comes from the stride, not from a format.
						D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = DXGI_FORMAT_UNKNOWN;
						ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = count;
						ud.Buffer.Flags = 0;
						hr = device->CreateUnorderedAccessView(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = DXGI_FORMAT_UNKNOWN;
						sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
						sd.Buffer.FirstElement = 0;
						sd.Buffer.NumElements = count;
						hr = device->CreateShaderResourceView(buffer, &sd, &srv);
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

				ID3D11UnorderedAccessView* UAV() const { return uav; }
				ID3D11ShaderResourceView* SRV() const { return srv; }
			};

			//A GPU-written *typed* buffer: `RWBuffer<uint>` to the writer, `Buffer<uint>`
			//to the reader. The counterpart of RWStructuredBuffer for the case where the
			//element is a scalar the format already describes, which is what makes
			//Clear() possible - ClearUnorderedAccessViewUint needs a typed or raw view
			//and does nothing useful on a structured one.
			class RWTypedBuffer {
			private:
				ID3D11Buffer* buffer = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
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
				HRESULT Init(uint32_t element_count, DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
					uint32_t element_bytes = 4) {
					Release();
					ID3D11Device* device = Core::DXCore::Get()->device;
					count = element_count;

					D3D11_BUFFER_DESC bd = {};
					bd.Usage = D3D11_USAGE_DEFAULT;
					bd.ByteWidth = element_bytes * count;
					bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
					bd.CPUAccessFlags = 0;
					bd.MiscFlags = 0;
					bd.StructureByteStride = 0;

					HRESULT hr = device->CreateBuffer(&bd, nullptr, &buffer);
					if (FAILED(hr)) { goto end; }

					{
						D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
						ud.Format = format;
						ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
						ud.Buffer.FirstElement = 0;
						ud.Buffer.NumElements = count;
						ud.Buffer.Flags = 0;
						hr = device->CreateUnorderedAccessView(buffer, &ud, &uav);
						if (FAILED(hr)) { goto end; }
					}
					{
						D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
						sd.Format = format;
						sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
						sd.Buffer.FirstElement = 0;
						sd.Buffer.NumElements = count;
						hr = device->CreateShaderResourceView(buffer, &sd, &srv);
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

				ID3D11UnorderedAccessView* UAV() const { return uav; }
				ID3D11ShaderResourceView* SRV() const { return srv; }
			};
		}
	}
}
