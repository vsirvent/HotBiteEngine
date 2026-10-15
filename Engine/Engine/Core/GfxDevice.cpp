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

#include "GfxDevice.h"
#include "DxcSupport.h"
#include <chrono>
#include "Log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace HotBite {
	namespace Engine {
		namespace Core {

			GfxDevice* GfxDevice::instance = nullptr;

						namespace {
							//The debug layer ends a process that makes an invalid call with an exception (0x87D for D3D12, 0x87A for
							//DXGI) whose reason is only in its message queue. This writes the queue to the log as the exception is
							//raised, before the crash handler (or the OS) takes the process away.
							LONG CALLBACK DebugLayerException(PEXCEPTION_POINTERS info) {
								const DWORD code = info->ExceptionRecord->ExceptionCode;
								if ((code == 0x87D || code == 0x87A) && GfxDevice::Get() != nullptr) {
									GfxDevice::Get()->DrainDebugMessages();
								}
								return EXCEPTION_CONTINUE_SEARCH;
							}
						}

			namespace {
				bool EnvFlag(const char* name) {
					char value[8] = {};
					size_t len = 0;
					if (getenv_s(&len, value, sizeof(value), name) != 0 || len == 0) {
						return false;
					}
					return value[0] == '1';
				}

				D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) {
					D3D12_HEAP_PROPERTIES p = {};
					p.Type = type;
					p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
					p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
					p.CreationNodeMask = 1;
					p.VisibleNodeMask = 1;
					return p;
				}

				D3D12_RESOURCE_DESC BufferDesc(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
					D3D12_RESOURCE_DESC d = {};
					d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					d.Alignment = 0;
					d.Width = size;
					d.Height = 1;
					d.DepthOrArraySize = 1;
					d.MipLevels = 1;
					d.Format = DXGI_FORMAT_UNKNOWN;
					d.SampleDesc.Count = 1;
					d.SampleDesc.Quality = 0;
					d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
					d.Flags = flags;
					return d;
				}

				D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, UINT subresource,
					D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
					D3D12_RESOURCE_BARRIER b = {};
					b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					b.Transition.pResource = resource;
					b.Transition.Subresource = subresource;
					b.Transition.StateBefore = before;
					b.Transition.StateAfter = after;
					return b;
				}

				UINT PlaneCount(ID3D12Device* device, DXGI_FORMAT format) {
					D3D12_FEATURE_DATA_FORMAT_INFO info = {};
					info.Format = format;
					if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info))) ||
						info.PlaneCount == 0) {
						return 1;
					}
					return info.PlaneCount;
				}
			}

			//-------------------------------------------------------------------------------------------
			// GfxObject and views
			//-------------------------------------------------------------------------------------------

			LONG GfxObject::Release() {
				const LONG remaining = refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (remaining == 0) {
					GfxDevice* device = GfxDevice::Get();
					if (gpu_owned && device != nullptr) {
						device->Defer(this);
					}
					else {
						delete this;
					}
				}
				return remaining;
			}

			GfxView::~GfxView() {
				GfxDevice* device = GfxDevice::Get();
				if (device != nullptr && pool_index != ~0u) {
					switch (pool_type) {
					case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: device->pool_rtv.Free(pool_index); break;
					case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: device->pool_dsv.Free(pool_index); break;
					default: device->pool_cbv_srv_uav.Free(pool_index); break;
					}
				}
				if (resource != nullptr) {
					resource->Release();
				}
			}

			GfxSampler::GfxSampler() {
				gpu_owned = false;
			}

			GfxSampler::~GfxSampler() {
				GfxDevice* device = GfxDevice::Get();
				if (device != nullptr && pool_index != ~0u) {
					device->pool_sampler.Free(pool_index);
				}
			}

			//-------------------------------------------------------------------------------------------
			// Descriptor pool
			//-------------------------------------------------------------------------------------------

			bool GfxDescriptorPool::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count) {
				D3D12_DESCRIPTOR_HEAP_DESC d = {};
				d.Type = type;
				d.NumDescriptors = count;
				d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				if (FAILED(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap)))) {
					return false;
				}
				base = heap->GetCPUDescriptorHandleForHeapStart();
				increment = device->GetDescriptorHandleIncrementSize(type);
				capacity = count;
				next = 0;
				free_list.clear();
				return true;
			}

			bool GfxDescriptorPool::Alloc(D3D12_CPU_DESCRIPTOR_HANDLE& handle, UINT& index) {
				std::lock_guard<std::mutex> lock(mutex);
				if (!free_list.empty()) {
					index = free_list.back();
					free_list.pop_back();
				}
				else if (next < capacity) {
					index = next++;
				}
				else {
					return false;
				}
				handle = Handle(index);
				return true;
			}

			void GfxDescriptorPool::Free(UINT index) {
				std::lock_guard<std::mutex> lock(mutex);
				free_list.push_back(index);
			}

			//-------------------------------------------------------------------------------------------
			// Device
			//-------------------------------------------------------------------------------------------

			GfxDevice::~GfxDevice() {
				Shutdown();
			}

			HRESULT GfxDevice::Init() {
				instance = this;
				HRESULT hr = S_OK;

				UINT dxgi_flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
				const bool want_debug = true;
#else
				const bool want_debug = EnvFlag("HOTBITE_D3D12_DEBUG");
#endif
				if (want_debug) {
					ComPtr<ID3D12Debug> debug;
					if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
						debug->EnableDebugLayer();
						debug_layer = true;
												dxgi_flags |= DXGI_CREATE_FACTORY_DEBUG;
												AddVectoredExceptionHandler(1, DebugLayerException);
						if (EnvFlag("HOTBITE_D3D12_GBV")) {
							ComPtr<ID3D12Debug1> debug1;
							if (SUCCEEDED(debug.As(&debug1))) {
								debug1->SetEnableGPUBasedValidation(TRUE);
								LOG_INFO("GfxDevice: GPU based validation on");
							}
						}
					}
					else {
						LOG_WARN("GfxDevice: the D3D12 debug layer was asked for but is not installed (Graphics Tools)");
					}
				}

				hr = CreateDXGIFactory2(dxgi_flags, IID_PPV_ARGS(&factory));
				if (FAILED(hr)) {
					LOG_ERROR("GfxDevice: CreateDXGIFactory2 failed (0x%08X)", (unsigned)hr);
					return hr;
				}

				{
					ComPtr<IDXGIFactory5> factory5;
					BOOL tearing = FALSE;
					if (SUCCEEDED(factory.As(&factory5)) &&
						SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)))) {
						allow_tearing = tearing != FALSE;
					}
				}

				//The adapter with the most dedicated memory, as the D3D11 path chose it.
				{
					SIZE_T best_memory = 0;
					for (UINT i = 0;; ++i) {
						ComPtr<IDXGIAdapter1> candidate;
						if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) {
							break;
						}
						DXGI_ADAPTER_DESC1 ad;
						candidate->GetDesc1(&ad);
						if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
							continue;
						}
						if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
							continue;
						}
						if (adapter == nullptr || ad.DedicatedVideoMemory > best_memory) {
							adapter = candidate;
							best_memory = ad.DedicatedVideoMemory;
							char name[128] = {};
							size_t converted = 0;
							wcstombs_s(&converted, name, sizeof(name), ad.Description, _TRUNCATE);
							adapter_name = name;
						}
					}
				}
				if (adapter == nullptr) {
					LOG_ERROR("GfxDevice: no adapter supports Direct3D 12 (feature level 12_0)");
					return DXGI_ERROR_UNSUPPORTED;
				}

				hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
				if (FAILED(hr)) {
					LOG_ERROR("GfxDevice: D3D12CreateDevice failed (0x%08X)", (unsigned)hr);
					return hr;
				}

				{
					static const D3D_FEATURE_LEVEL levels[] = {
						D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
					D3D12_FEATURE_DATA_FEATURE_LEVELS fl = {};
					fl.NumFeatureLevels = _countof(levels);
					fl.pFeatureLevelsRequested = levels;
					if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) {
						feature_level = fl.MaxSupportedFeatureLevel;
					}
					D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
					if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
						options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2) {
						LOG_ERROR("GfxDevice: %s does not support resource binding tier 2", adapter_name.c_str());
						return DXGI_ERROR_UNSUPPORTED;
					}
				}
				LOG_INFO("GfxDevice: D3D12 feature level %u_%u on %s%s%s",
					(unsigned)(feature_level >> 12), (unsigned)((feature_level >> 8) & 0xF), adapter_name.c_str(),
					allow_tearing ? ", tearing allowed" : "", debug_layer ? ", debug layer" : "");

				if (debug_layer) {
					ComPtr<ID3D12InfoQueue> info;
							if (SUCCEEDED(device.As(&info))) {
								//Messages are collected and written to the log (DrainDebugMessages) rather than raising an exception
								//the moment they are produced, which would take the process down before the log could say why.
								info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
								info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
								info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
						//Warnings about clear colours that differ from the one a texture was created with are
						//about a (small) performance fast path, not about correctness.
						D3D12_MESSAGE_ID hidden[] = {
							D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
							D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
						};
						D3D12_INFO_QUEUE_FILTER filter = {};
						filter.DenyList.NumIDs = _countof(hidden);
						filter.DenyList.pIDList = hidden;
						info->AddStorageFilterEntries(&filter);
					}
				}

				D3D12_COMMAND_QUEUE_DESC qd = {};
				qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				hr = device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
				if (FAILED(hr)) { return hr; }
				hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
				if (FAILED(hr)) { return hr; }
				fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
				hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&immediate_fence));
				if (FAILED(hr)) { return hr; }

				inc_cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				inc_sampler = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				if (!pool_cbv_srv_uav.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u << 19) ||
					!pool_rtv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1u << 14) ||
					!pool_dsv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1u << 12) ||
					!pool_sampler.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1u << 12)) {
					LOG_ERROR("GfxDevice: could not create the descriptor pools");
					return E_OUTOFMEMORY;
				}

				for (UINT i = 0; i < IMMEDIATE_LISTS; ++i) {
					hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&immediate_allocators[i]));
					if (FAILED(hr)) { return hr; }
					hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, immediate_allocators[i].Get(), nullptr,
						IID_PPV_ARGS(&immediate_lists[i]));
					if (FAILED(hr)) { return hr; }
					immediate_lists[i]->Close();
				}

								queue->GetTimestampFrequency(&timestamp_frequency);
								CreateNullDescriptors();
								return S_OK;
			}

			void GfxDevice::CreateNullDescriptors() {
				//Null SRVs/UAVs: a shader that reads a slot nothing was bound to must get zeros, as in D3D11. The
				//descriptor has to be of the kind the shader reads the slot as, so there is one per dimension.
				D3D12_DESCRIPTOR_HEAP_DESC hd = {};
				hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				hd.NumDescriptors = 16 + 8 + 1;
				hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&null_heap));
				D3D12_CPU_DESCRIPTOR_HANDLE h = null_heap->GetCPUDescriptorHandleForHeapStart();

				for (UINT i = 0; i < 16; ++i) {
					null_srv[i] = { h.ptr + (SIZE_T)i * inc_cbv_srv_uav };
					D3D12_SRV_DIMENSION dim = (D3D12_SRV_DIMENSION)i;
					if (i == 0 || i > D3D12_SRV_DIMENSION_TEXTURECUBEARRAY) {
						dim = D3D12_SRV_DIMENSION_TEXTURE2D;
					}
					D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
					sd.Format = DXGI_FORMAT_R32_UINT;
					sd.ViewDimension = dim;
					sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					switch (dim) {
					case D3D12_SRV_DIMENSION_BUFFER: sd.Buffer.NumElements = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE1D: sd.Texture1D.MipLevels = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE1DARRAY: sd.Texture1DArray.MipLevels = 1; sd.Texture1DArray.ArraySize = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE2D: sd.Texture2D.MipLevels = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE2DARRAY: sd.Texture2DArray.MipLevels = 1; sd.Texture2DArray.ArraySize = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE2DMS: break;
					case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY: sd.Texture2DMSArray.ArraySize = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE3D: sd.Texture3D.MipLevels = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURECUBE: sd.TextureCube.MipLevels = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY: sd.TextureCubeArray.MipLevels = 1; sd.TextureCubeArray.NumCubes = 1; break;
					default: break;
					}
					device->CreateShaderResourceView(nullptr, &sd, null_srv[i]);
				}

				for (UINT i = 0; i < 8; ++i) {
					null_uav[i] = { h.ptr + (SIZE_T)(16 + i) * inc_cbv_srv_uav };
					D3D12_UAV_DIMENSION dim = (D3D12_UAV_DIMENSION)i;
					if (i == 0 || i > D3D12_UAV_DIMENSION_TEXTURE2DARRAY) {
						dim = D3D12_UAV_DIMENSION_TEXTURE2D;
					}
					D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
					ud.Format = DXGI_FORMAT_R32_UINT;
					ud.ViewDimension = dim;
					switch (dim) {
					case D3D12_UAV_DIMENSION_BUFFER: ud.Buffer.NumElements = 1; break;
					case D3D12_UAV_DIMENSION_TEXTURE2DARRAY: ud.Texture2DArray.ArraySize = 1; break;
					case D3D12_UAV_DIMENSION_TEXTURE1DARRAY: ud.Texture1DArray.ArraySize = 1; break;
					default: break;
					}
					device->CreateUnorderedAccessView(nullptr, nullptr, &ud, null_uav[i]);
				}

				//A constant buffer slot nothing was bound to reads zero. An upload buffer nothing ever
				//writes is zero-filled by the driver.
				const UINT64 zero_size = 65536;
				D3D12_HEAP_PROPERTIES up = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
				D3D12_RESOURCE_DESC bd = BufferDesc(zero_size);
				device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr, IID_PPV_ARGS(&zero_buffer));
				void* mapped = nullptr;
				D3D12_RANGE none = { 0, 0 };
				if (zero_buffer != nullptr && SUCCEEDED(zero_buffer->Map(0, &none, &mapped))) {
					memset(mapped, 0, (size_t)zero_size);
					zero_buffer->Unmap(0, nullptr);
				}
				null_cbv = { h.ptr + (SIZE_T)(16 + 8) * inc_cbv_srv_uav };
				D3D12_CONSTANT_BUFFER_VIEW_DESC cd = {};
				cd.BufferLocation = zero_buffer->GetGPUVirtualAddress();
				cd.SizeInBytes = (UINT)zero_size;
				device->CreateConstantBufferView(&cd, null_cbv);

				D3D12_DESCRIPTOR_HEAP_DESC rh = {};
					rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
					rh.NumDescriptors = 1;
					rh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
					device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&null_rtv_heap));
					null_rtv = null_rtv_heap->GetCPUDescriptorHandleForHeapStart();
					D3D12_RENDER_TARGET_VIEW_DESC rd = {};
					rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
					device->CreateRenderTargetView(nullptr, &rd, null_rtv);

					D3D12_DESCRIPTOR_HEAP_DESC sh = {};
					sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
				sh.NumDescriptors = 1;
				sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&null_sampler_heap));
				null_sampler = null_sampler_heap->GetCPUDescriptorHandleForHeapStart();
				D3D12_SAMPLER_DESC sd = {};
				sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
				sd.MaxLOD = D3D12_FLOAT32_MAX;
				sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
				device->CreateSampler(&sd, null_sampler);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE GfxDevice::NullSRV(D3D12_SRV_DIMENSION dimension) const {
				UINT i = (UINT)dimension;
				if (i == 0 || i >= 16) {
					i = D3D12_SRV_DIMENSION_TEXTURE2D;
				}
				return null_srv[i];
			}

			D3D12_CPU_DESCRIPTOR_HANDLE GfxDevice::NullUAV(D3D12_UAV_DIMENSION dimension) const {
				UINT i = (UINT)dimension;
				if (i == 0 || i >= 8) {
					i = (dimension == D3D12_UAV_DIMENSION_TEXTURE3D) ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
				}
				return null_uav[i];
			}

			void GfxDevice::Shutdown() {
				if (device == nullptr) {
					instance = (instance == this) ? nullptr : instance;
					return;
				}
				WaitIdle();
				//Everything the GPU could have been using is idle now: destroy it all, however recent.
				for (;;) {
					std::deque<Deferred> items;
					{
						std::lock_guard<std::mutex> lock(defer_mutex);
						items.swap(deferred);
					}
					if (items.empty()) {
						break;
					}
					for (Deferred& d : items) {
						delete d.object;
					}
				}
				pending_uploads.clear();
				DrainDebugMessages();
				if (fence_event != nullptr) {
					CloseHandle(fence_event);
					fence_event = nullptr;
				}
				if (instance == this) {
					instance = nullptr;
				}
				for (UINT i = 0; i < IMMEDIATE_LISTS; ++i) {
					immediate_lists[i].Reset();
					immediate_allocators[i].Reset();
				}
				immediate_fence.Reset();
				fence.Reset();
				queue.Reset();
				zero_buffer.Reset();
				null_heap.Reset();
				null_sampler_heap.Reset();
				device.Reset();
				adapter.Reset();
				factory.Reset();
			}

			//-------------------------------------------------------------------------------------------
			// Frame fences and deferred destruction
			//-------------------------------------------------------------------------------------------

			uint64_t GfxDevice::SubmitFrame(ID3D12CommandList* list) {
				std::lock_guard<std::mutex> lock(queue_mutex);
				ID3D12CommandList* lists[] = { list };
				queue->ExecuteCommandLists(1, lists);
				const uint64_t serial = recording_serial.fetch_add(1);
				queue->Signal(fence.Get(), serial);
				return serial;
			}

			void GfxDevice::WaitFrame(uint64_t serial) {
				if (fence->GetCompletedValue() >= serial) {
					return;
				}
				const auto begin = std::chrono::steady_clock::now();
				fence->SetEventOnCompletion(serial, fence_event);
				WaitForSingleObject(fence_event, INFINITE);
				const uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
				std::lock_guard<std::mutex> lock(wait_stats_mutex);
				wait_stats.frame_ns += ns;
				++wait_stats.frame_waits;
			}

			GfxDevice::WaitStats GfxDevice::TakeWaitStats() {
				std::lock_guard<std::mutex> lock(wait_stats_mutex);
				const WaitStats taken = wait_stats;
				wait_stats = WaitStats();
				return taken;
			}

			void GfxDevice::WaitIdle() {
				if (queue == nullptr) {
					return;
				}
				std::lock_guard<std::mutex> lock(queue_mutex);
				//A value no frame will use, signalled behind everything that was submitted.
				const uint64_t value = recording_serial.fetch_add(1);
				queue->Signal(fence.Get(), value);
				if (fence->GetCompletedValue() < value) {
					fence->SetEventOnCompletion(value, fence_event);
					WaitForSingleObject(fence_event, INFINITE);
				}
				if (immediate_fence != nullptr && immediate_fence->GetCompletedValue() < immediate_value) {
					immediate_fence->SetEventOnCompletion(immediate_value, fence_event);
					WaitForSingleObject(fence_event, INFINITE);
				}
			}

			void GfxDevice::Defer(GfxObject* object) {
				//The frame being recorded is the last one that can have used it.
				std::lock_guard<std::mutex> lock(defer_mutex);
				deferred.push_back({ recording_serial.load(), object });
			}

			void GfxDevice::CollectGarbage() {
				std::vector<GfxObject*> dead;
				const uint64_t completed = fence->GetCompletedValue();
				{
					std::lock_guard<std::mutex> lock(defer_mutex);
					while (!deferred.empty() && deferred.front().serial <= completed) {
						dead.push_back(deferred.front().object);
						deferred.pop_front();
					}
				}
				//Outside the lock: destroying a view releases its resource, which may defer again.
				for (GfxObject* o : dead) {
					delete o;
				}
				{
					std::lock_guard<std::mutex> lock(immediate_mutex);
					const uint64_t done = immediate_fence->GetCompletedValue();
					while (!pending_uploads.empty() && pending_uploads.front().value <= done) {
						pending_uploads.pop_front();
					}
				}
			}

			//HOTBITE_GFX_TRACE_CREATE=<first frame>: what is created with contents after that frame, the first 100 times.
			void GfxDevice::TraceCreation(const char* what, int a, UINT64 b, UINT64 c) {
				static const uint64_t from = []() {
					char value[32] = {};
					size_t len = 0;
					if (getenv_s(&len, value, sizeof(value), "HOTBITE_GFX_TRACE_CREATE") == 0 && len > 1) {
						return (uint64_t)_strtoui64(value, nullptr, 10);
					}
					return (uint64_t)0;
				}();
				static std::atomic<int> reported{ 0 };
				if (from != 0 && recording_serial.load() >= from && reported.fetch_add(1) < 100) {
					LOG_INFO("gfx create with contents: %s a=%d b=%llu c=%llu (frame %llu)", what, a, (unsigned long long)b, (unsigned long long)c,
						(unsigned long long)recording_serial.load());
				}
			}

			void GfxDevice::ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record,
				std::vector<ComPtr<ID3D12Resource>>&& keep_alive) {
				std::lock_guard<std::mutex> lock(immediate_mutex);
					//The allocator can only be reset once the list that used it has finished; the ring makes that rarely a wait.
					const UINT slot = immediate_next;
					immediate_next = (immediate_next + 1) % IMMEDIATE_LISTS;
					uint64_t waited_ns = 0;
					bool waited = false;
					if (immediate_fence->GetCompletedValue() < immediate_list_value[slot]) {
						const auto begin = std::chrono::steady_clock::now();
						immediate_fence->SetEventOnCompletion(immediate_list_value[slot], fence_event);
						WaitForSingleObject(fence_event, INFINITE);
						waited_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
						waited = true;
					}
					{
						std::lock_guard<std::mutex> stats_lock(wait_stats_mutex);
						++wait_stats.immediate_calls;
						wait_stats.immediate_ns += waited_ns;
						wait_stats.immediate_waits += waited ? 1u : 0u;
					}
				immediate_allocators[slot]->Reset();
				immediate_lists[slot]->Reset(immediate_allocators[slot].Get(), nullptr);
				record(immediate_lists[slot].Get());
				immediate_lists[slot]->Close();
				ID3D12CommandList* lists[] = { immediate_lists[slot].Get() };
				{
					std::lock_guard<std::mutex> qlock(queue_mutex);
					queue->ExecuteCommandLists(1, lists);
					queue->Signal(immediate_fence.Get(), ++immediate_value);
				}
				immediate_list_value[slot] = immediate_value;
				pending_uploads.push_back({ immediate_value, std::move(keep_alive) });
			}

			//-------------------------------------------------------------------------------------------
			// Resources
			//-------------------------------------------------------------------------------------------

			HRESULT GfxDevice::CreateBuffer(GfxBufferKind kind, UINT64 size, UINT stride, D3D12_RESOURCE_FLAGS flags,
				const void* initial_data, D3D12_RESOURCE_STATES resting_state, GfxBuffer** out) {
				*out = nullptr;
				GfxBuffer* buffer = new GfxBuffer();
				buffer->kind = kind;
				buffer->size = size;
				buffer->stride = stride;
				buffer->subresource_count = 1;

				if (kind == GfxBufferKind::Constant || kind == GfxBufferKind::Dynamic) {
					//Lives in the frame's upload ring; see GfxContext::UpdateBuffer. The contents, if any, are
					//the first publication.
					buffer->heap_type = D3D12_HEAP_TYPE_UPLOAD;
					buffer->desc = BufferDesc(size);
					if (kind == GfxBufferKind::Constant) {
						buffer->shadow.assign((size_t)size, 0);
						if (initial_data != nullptr) {
							memcpy(buffer->shadow.data(), initial_data, (size_t)size);
						}
					}
					*out = buffer;
					return S_OK;
				}

				buffer->gpu_owned = true;
				buffer->desc = BufferDesc(size, flags);
				D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT;
					//A buffer is created in COMMON whatever state is asked for (D3D12 ignores it), so that is what is tracked
					//until the first use moves it; only the heaps with a fixed state differ.
					D3D12_RESOURCE_STATES creation_state = D3D12_RESOURCE_STATE_COMMON;
				if (kind == GfxBufferKind::Upload) {
					heap = D3D12_HEAP_TYPE_UPLOAD;
					creation_state = D3D12_RESOURCE_STATE_GENERIC_READ;
				}
				else if (kind == GfxBufferKind::Readback) {
					heap = D3D12_HEAP_TYPE_READBACK;
					creation_state = D3D12_RESOURCE_STATE_COPY_DEST;
				}
				buffer->heap_type = heap;
				buffer->resting_state = resting_state;
				D3D12_HEAP_PROPERTIES hp = HeapProps(heap);
				HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &buffer->desc, creation_state,
					nullptr, IID_PPV_ARGS(&buffer->resource));
				if (FAILED(hr)) {
					LOG_ERROR("GfxDevice: CreateBuffer(%llu bytes) failed (0x%08X)", (unsigned long long)size, (unsigned)hr);
					buffer->Release();
					return hr;
				}
				buffer->SetAllState(creation_state);

				if (kind == GfxBufferKind::Upload || kind == GfxBufferKind::Readback) {
					D3D12_RANGE read_range = { 0, (kind == GfxBufferKind::Readback) ? (SIZE_T)size : 0 };
					hr = buffer->resource->Map(0, &read_range, &buffer->mapped);
					if (FAILED(hr)) {
						buffer->Release();
						return hr;
					}
					if (initial_data != nullptr && kind == GfxBufferKind::Upload) {
						memcpy(buffer->mapped, initial_data, (size_t)size);
					}
				}
				else if (initial_data != nullptr) {
					TraceCreation("buffer", (int)kind, size, stride);
					ComPtr<ID3D12Resource> staging;
					D3D12_HEAP_PROPERTIES up = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
					D3D12_RESOURCE_DESC sd = BufferDesc(size);
					hr = device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_GENERIC_READ,
						nullptr, IID_PPV_ARGS(&staging));
					if (FAILED(hr)) {
						buffer->Release();
						return hr;
					}
					void* mapped = nullptr;
					D3D12_RANGE none = { 0, 0 };
					staging->Map(0, &none, &mapped);
					memcpy(mapped, initial_data, (size_t)size);
					staging->Unmap(0, nullptr);
					ID3D12Resource* target = buffer->resource.Get();
					ExecuteImmediate([&](ID3D12GraphicsCommandList* list) {
						list->CopyBufferRegion(target, 0, staging.Get(), 0, size);
						if (resting_state != D3D12_RESOURCE_STATE_COPY_DEST) {
							D3D12_RESOURCE_BARRIER b = TransitionBarrier(target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
								D3D12_RESOURCE_STATE_COPY_DEST, resting_state);
							list->ResourceBarrier(1, &b);
						}
						}, { staging });
					buffer->SetAllState(resting_state);
				}
				*out = buffer;
				return S_OK;
			}

			HRESULT GfxDevice::CreateTexture2D(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial_state,
				const D3D12_CLEAR_VALUE* clear_value, const GfxSubresourceData* data, UINT data_count, GfxTexture** out) {
				*out = nullptr;
				const bool has_data = data != nullptr && data_count > 0;
				GfxTexture* texture = new GfxTexture();
				texture->gpu_owned = true;
				texture->desc = desc;
				texture->width = (UINT)desc.Width;
				texture->height = desc.Height;
				texture->depth_or_array = desc.DepthOrArraySize;
				texture->mip_levels = desc.MipLevels;
				texture->format = desc.Format;
				texture->heap_type = D3D12_HEAP_TYPE_DEFAULT;
				const UINT planes = PlaneCount(device.Get(), desc.Format);
				texture->subresource_count = (UINT)desc.MipLevels * (UINT)desc.DepthOrArraySize * planes;

				const D3D12_RESOURCE_STATES creation_state = has_data ? D3D12_RESOURCE_STATE_COPY_DEST : initial_state;
				if (has_data) {
					TraceCreation("texture", (int)desc.Format, desc.Width, desc.Height);
				}
				D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
				HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &texture->desc, creation_state,
					clear_value, IID_PPV_ARGS(&texture->resource));
				if (FAILED(hr)) {
					LOG_ERROR("GfxDevice: CreateTexture2D(%ux%u, format %u) failed (0x%08X)",
						(unsigned)desc.Width, (unsigned)desc.Height, (unsigned)desc.Format, (unsigned)hr);
					texture->Release();
					return hr;
				}
				texture->SetAllState(creation_state);

				if (has_data) {
					const UINT count = (std::min<UINT>)(data_count, texture->subresource_count);
					std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
					std::vector<UINT> rows(count);
					std::vector<UINT64> row_sizes(count);
					UINT64 total = 0;
					device->GetCopyableFootprints(&texture->desc, 0, count, 0, layouts.data(), rows.data(),
						row_sizes.data(), &total);

					ComPtr<ID3D12Resource> staging;
					D3D12_HEAP_PROPERTIES up = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
					D3D12_RESOURCE_DESC sd = BufferDesc(total);
					hr = device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_GENERIC_READ,
						nullptr, IID_PPV_ARGS(&staging));
					if (FAILED(hr)) {
						texture->Release();
						return hr;
					}
					uint8_t* mapped = nullptr;
					D3D12_RANGE none = { 0, 0 };
					staging->Map(0, &none, (void**)&mapped);
					for (UINT i = 0; i < count; ++i) {
						const uint8_t* src = (const uint8_t*)data[i].data;
						if (src == nullptr) {
							continue;
						}
						const UINT src_pitch = (data[i].row_pitch != 0) ? data[i].row_pitch : (UINT)row_sizes[i];
						uint8_t* dst = mapped + layouts[i].Offset;
						for (UINT r = 0; r < rows[i]; ++r) {
							memcpy(dst + (size_t)r * layouts[i].Footprint.RowPitch, src + (size_t)r * src_pitch,
								(size_t)row_sizes[i]);
						}
					}
					staging->Unmap(0, nullptr);

					ID3D12Resource* target = texture->resource.Get();
					ExecuteImmediate([&](ID3D12GraphicsCommandList* list) {
						for (UINT i = 0; i < count; ++i) {
							D3D12_TEXTURE_COPY_LOCATION dst = {};
							dst.pResource = target;
							dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
							dst.SubresourceIndex = i;
							D3D12_TEXTURE_COPY_LOCATION src = {};
							src.pResource = staging.Get();
							src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
							src.PlacedFootprint = layouts[i];
							list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
						}
						D3D12_RESOURCE_BARRIER b = TransitionBarrier(target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
							D3D12_RESOURCE_STATE_COPY_DEST, GFX_STATE_SHADER_READ);
						list->ResourceBarrier(1, &b);
						}, { staging });
					texture->SetAllState(GFX_STATE_SHADER_READ);
				}
				*out = texture;
				return S_OK;
			}

			HRESULT GfxDevice::WrapTexture(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, GfxTexture** out) {
				GfxTexture* texture = new GfxTexture();
				texture->gpu_owned = true;
				texture->resource = resource;
				texture->desc = resource->GetDesc();
				texture->width = (UINT)texture->desc.Width;
				texture->height = texture->desc.Height;
				texture->depth_or_array = texture->desc.DepthOrArraySize;
				texture->mip_levels = texture->desc.MipLevels;
				texture->format = texture->desc.Format;
				texture->subresource_count = (UINT)texture->desc.MipLevels * (UINT)texture->desc.DepthOrArraySize *
					PlaneCount(device.Get(), texture->desc.Format);
				texture->SetAllState(state);
				*out = texture;
				return S_OK;
			}

			//-------------------------------------------------------------------------------------------
			// Views
			//-------------------------------------------------------------------------------------------

			void GfxDevice::SetViewRange(GfxView* view, GfxResource* resource, UINT first_mip, UINT mip_count,
				UINT first_slice, UINT slice_count) {
				UINT mips = 1;
				UINT slices = 1;
				if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
					mips = resource->desc.MipLevels;
					slices = (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : resource->desc.DepthOrArraySize;
				}
				view->mip_levels_total = mips;
				view->array_size_total = slices;
				view->first_mip = (std::min)(first_mip, mips - 1);
				view->mip_count = (mip_count == (UINT)-1 || view->first_mip + mip_count > mips) ? mips - view->first_mip : mip_count;
				view->first_slice = (std::min)(first_slice, slices - 1);
				view->slice_count = (slice_count == (UINT)-1 || view->first_slice + slice_count > slices) ? slices - view->first_slice : slice_count;
				view->covers_all = view->first_mip == 0 && view->mip_count == mips && view->first_slice == 0 && view->slice_count == slices;
			}

			HRESULT GfxDevice::CreateSRV(GfxResource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, GfxSRV** out) {
				*out = nullptr;
				GfxSRV* view = new GfxSRV();
				view->gpu_owned = true;
				view->resource = resource;
				resource->AddRef();
				view->pool_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				if (!pool_cbv_srv_uav.Alloc(view->cpu, view->pool_index)) {
					LOG_ERROR("GfxDevice: out of SRV/UAV descriptors");
					view->Release();
					return E_OUTOFMEMORY;
				}
				view->format = desc ? desc->Format : resource->desc.Format;
				device->CreateShaderResourceView(resource->Get(), desc, view->cpu);

				UINT first_mip = 0, mip_count = (UINT)-1, first_slice = 0, slice_count = (UINT)-1;
				view->dimension = desc ? desc->ViewDimension : D3D12_SRV_DIMENSION_TEXTURE2D;
				if (desc != nullptr) {
					switch (desc->ViewDimension) {
					case D3D12_SRV_DIMENSION_TEXTURE1D:
						first_mip = desc->Texture1D.MostDetailedMip; mip_count = desc->Texture1D.MipLevels; slice_count = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
						first_mip = desc->Texture1DArray.MostDetailedMip; mip_count = desc->Texture1DArray.MipLevels;
						first_slice = desc->Texture1DArray.FirstArraySlice; slice_count = desc->Texture1DArray.ArraySize; break;
					case D3D12_SRV_DIMENSION_TEXTURE2D:
						first_mip = desc->Texture2D.MostDetailedMip; mip_count = desc->Texture2D.MipLevels; slice_count = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
						first_mip = desc->Texture2DArray.MostDetailedMip; mip_count = desc->Texture2DArray.MipLevels;
						first_slice = desc->Texture2DArray.FirstArraySlice; slice_count = desc->Texture2DArray.ArraySize; break;
					case D3D12_SRV_DIMENSION_TEXTURE3D:
						first_mip = desc->Texture3D.MostDetailedMip; mip_count = desc->Texture3D.MipLevels; slice_count = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURECUBE:
						first_mip = desc->TextureCube.MostDetailedMip; mip_count = desc->TextureCube.MipLevels; slice_count = 6; break;
					case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
						first_mip = desc->TextureCubeArray.MostDetailedMip; mip_count = desc->TextureCubeArray.MipLevels;
						first_slice = desc->TextureCubeArray.First2DArrayFace; slice_count = desc->TextureCubeArray.NumCubes * 6; break;
					case D3D12_SRV_DIMENSION_TEXTURE2DMS:
						slice_count = 1; mip_count = 1; break;
					case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
						first_slice = desc->Texture2DMSArray.FirstArraySlice; slice_count = desc->Texture2DMSArray.ArraySize; mip_count = 1; break;
					default:
						break;
					}
				}
				SetViewRange(view, resource, first_mip, mip_count, first_slice, slice_count);
				*out = view;
				return S_OK;
			}

			HRESULT GfxDevice::CreateUAV(GfxResource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUAV** out) {
				*out = nullptr;
				GfxUAV* view = new GfxUAV();
				view->gpu_owned = true;
				view->resource = resource;
				resource->AddRef();
				view->pool_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				if (!pool_cbv_srv_uav.Alloc(view->cpu, view->pool_index)) {
					LOG_ERROR("GfxDevice: out of SRV/UAV descriptors");
					view->Release();
					return E_OUTOFMEMORY;
				}
				view->format = desc ? desc->Format : resource->desc.Format;
					if (view->format != DXGI_FORMAT_UNKNOWN && view->format != DXGI_FORMAT_R32_TYPELESS) {
						//A shader that writes a typed UAV needs the format to support typed stores; if it does not the writes
						//vanish without a message, which is what a black frame looks like.
						D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = {};
						fs.Format = view->format;
						if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) &&
							(fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
							LOG_WARN("GfxDevice: format %u does not support typed UAV stores on %s", (unsigned)view->format, adapter_name.c_str());
						}
					}
					device->CreateUnorderedAccessView(resource->Get(), nullptr, desc, view->cpu);

				UINT first_mip = 0, mip_count = 1, first_slice = 0, slice_count = (UINT)-1;
				view->dimension = desc ? desc->ViewDimension : D3D12_UAV_DIMENSION_TEXTURE2D;
				if (desc != nullptr) {
					switch (desc->ViewDimension) {
					case D3D12_UAV_DIMENSION_TEXTURE1D:
						first_mip = desc->Texture1D.MipSlice; slice_count = 1; break;
					case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
						first_mip = desc->Texture1DArray.MipSlice; first_slice = desc->Texture1DArray.FirstArraySlice;
						slice_count = desc->Texture1DArray.ArraySize; break;
					case D3D12_UAV_DIMENSION_TEXTURE2D:
						first_mip = desc->Texture2D.MipSlice; slice_count = 1; break;
					case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
						first_mip = desc->Texture2DArray.MipSlice; first_slice = desc->Texture2DArray.FirstArraySlice;
						slice_count = desc->Texture2DArray.ArraySize; break;
					case D3D12_UAV_DIMENSION_TEXTURE3D:
						first_mip = desc->Texture3D.MipSlice; slice_count = 1; break;
					default:
						break;
					}
				}
				else if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
					slice_count = 1;
				}
				SetViewRange(view, resource, first_mip, mip_count, first_slice, slice_count);
				*out = view;
				return S_OK;
			}

			HRESULT GfxDevice::CreateRTV(GfxResource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc, GfxRTV** out) {
				*out = nullptr;
				GfxRTV* view = new GfxRTV();
				view->gpu_owned = true;
				view->resource = resource;
				resource->AddRef();
				view->pool_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				if (!pool_rtv.Alloc(view->cpu, view->pool_index)) {
					LOG_ERROR("GfxDevice: out of RTV descriptors");
					view->Release();
					return E_OUTOFMEMORY;
				}
				view->format = desc ? desc->Format : resource->desc.Format;
				device->CreateRenderTargetView(resource->Get(), desc, view->cpu);

				UINT first_mip = 0, first_slice = 0, slice_count = 1;
				if (desc != nullptr) {
					switch (desc->ViewDimension) {
					case D3D12_RTV_DIMENSION_TEXTURE1D: first_mip = desc->Texture1D.MipSlice; break;
					case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
						first_mip = desc->Texture1DArray.MipSlice; first_slice = desc->Texture1DArray.FirstArraySlice;
						slice_count = desc->Texture1DArray.ArraySize; break;
					case D3D12_RTV_DIMENSION_TEXTURE2D: first_mip = desc->Texture2D.MipSlice; break;
					case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
						first_mip = desc->Texture2DArray.MipSlice; first_slice = desc->Texture2DArray.FirstArraySlice;
						slice_count = desc->Texture2DArray.ArraySize; break;
					case D3D12_RTV_DIMENSION_TEXTURE3D: first_mip = desc->Texture3D.MipSlice; break;
					default: break;
					}
				}
				SetViewRange(view, resource, first_mip, 1, first_slice, slice_count);
				*out = view;
				return S_OK;
			}

			HRESULT GfxDevice::CreateDSV(GfxResource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, GfxDSV** out) {
				*out = nullptr;
				GfxDSV* view = new GfxDSV();
				view->gpu_owned = true;
				view->resource = resource;
				resource->AddRef();
				view->pool_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
				if (!pool_dsv.Alloc(view->cpu, view->pool_index)) {
					LOG_ERROR("GfxDevice: out of DSV descriptors");
					view->Release();
					return E_OUTOFMEMORY;
				}
				view->format = desc ? desc->Format : resource->desc.Format;
				view->read_only = desc != nullptr && (desc->Flags & (D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL)) != 0;
				device->CreateDepthStencilView(resource->Get(), desc, view->cpu);

				UINT first_mip = 0, first_slice = 0, slice_count = 1;
				if (desc != nullptr) {
					switch (desc->ViewDimension) {
					case D3D12_DSV_DIMENSION_TEXTURE1D: first_mip = desc->Texture1D.MipSlice; break;
					case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
						first_mip = desc->Texture1DArray.MipSlice; first_slice = desc->Texture1DArray.FirstArraySlice;
						slice_count = desc->Texture1DArray.ArraySize; break;
					case D3D12_DSV_DIMENSION_TEXTURE2D: first_mip = desc->Texture2D.MipSlice; break;
					case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
						first_mip = desc->Texture2DArray.MipSlice; first_slice = desc->Texture2DArray.FirstArraySlice;
						slice_count = desc->Texture2DArray.ArraySize; break;
					default: break;
					}
				}
				SetViewRange(view, resource, first_mip, 1, first_slice, slice_count);
				*out = view;
				return S_OK;
			}

			//-------------------------------------------------------------------------------------------
			// State objects and shaders
			//-------------------------------------------------------------------------------------------

						GfxQueryHeap::~GfxQueryHeap() {
							if (readback != nullptr) {
								readback->Release();
							}
						}

						bool GfxQueryHeap::Ready() const {
							GfxDevice* device = GfxDevice::Get();
							return device != nullptr && ready_fence != 0 && device->IsFrameComplete(ready_fence);
						}

						HRESULT GfxDevice::CreateQueryHeap(D3D12_QUERY_TYPE type, UINT count, GfxQueryHeap** out) {
							*out = nullptr;
							D3D12_QUERY_HEAP_DESC qd = {};
							qd.Count = count;
							qd.NodeMask = 0;
							UINT bytes = 8;
							switch (type) {
							case D3D12_QUERY_TYPE_TIMESTAMP:
								qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
								bytes = sizeof(UINT64);
								break;
							case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
								qd.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
								bytes = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
								break;
							default:
								return E_INVALIDARG;
							}
							GfxQueryHeap* heap = new GfxQueryHeap();
							heap->query_type = type;
							heap->count = count;
							heap->bytes_per_query = bytes;
							HRESULT hr = device->CreateQueryHeap(&qd, IID_PPV_ARGS(&heap->heap));
							if (SUCCEEDED(hr)) {
								hr = CreateBuffer(GfxBufferKind::Readback, (UINT64)bytes * count, 0, D3D12_RESOURCE_FLAG_NONE, nullptr,
									D3D12_RESOURCE_STATE_COPY_DEST, &heap->readback);
							}
							if (FAILED(hr)) {
								heap->Release();
								return hr;
							}
							*out = heap;
							return S_OK;
						}

						HRESULT GfxDevice::CreateSampler(const D3D12_SAMPLER_DESC& desc, GfxSampler** out) {
				*out = nullptr;
				GfxSampler* sampler = new GfxSampler();
				sampler->desc = desc;
				sampler->id = NextStateId();
				if (!pool_sampler.Alloc(sampler->cpu, sampler->pool_index)) {
					LOG_ERROR("GfxDevice: out of sampler descriptors");
					sampler->Release();
					return E_OUTOFMEMORY;
				}
				device->CreateSampler(&desc, sampler->cpu);
				*out = sampler;
				return S_OK;
			}

			HRESULT GfxDevice::CreateRasterizerState(const D3D12_RASTERIZER_DESC& desc, bool scissor_enable, GfxRasterizerState** out) {
				GfxRasterizerState* s = new GfxRasterizerState();
				s->desc = desc;
				s->scissor_enable = scissor_enable;
				s->id = NextStateId();
				*out = s;
				return S_OK;
			}

			HRESULT GfxDevice::CreateBlendState(const D3D12_BLEND_DESC& desc, GfxBlendState** out) {
				GfxBlendState* s = new GfxBlendState();
				s->desc = desc;
				s->id = NextStateId();
				*out = s;
				return S_OK;
			}

			HRESULT GfxDevice::CreateDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& desc, GfxDepthStencilState** out) {
				GfxDepthStencilState* s = new GfxDepthStencilState();
				s->desc = desc;
				s->id = NextStateId();
				*out = s;
				return S_OK;
			}

			HRESULT GfxDevice::CreateInputLayout(const D3D12_INPUT_ELEMENT_DESC* elements, UINT count, GfxInputLayout** out) {
				GfxInputLayout* layout = new GfxInputLayout();
				layout->id = NextStateId();
				layout->names.reserve(count);
				layout->elements.assign(elements, elements + count);
				for (UINT i = 0; i < count; ++i) {
					layout->names.push_back(elements[i].SemanticName);
				}
				//Reserved up front, so the pointers stay valid.
				for (UINT i = 0; i < count; ++i) {
					layout->elements[i].SemanticName = layout->names[i].c_str();
				}
				*out = layout;
				return S_OK;
			}

			bool GfxDevice::CreateShader(GfxShader::Stage stage, const void* bytecode, size_t size, GfxShader** out) {
				*out = nullptr;
				ComPtr<ID3D12ShaderReflection> reflection;
				if (FAILED(DxcSupport::Reflect(bytecode, size, reflection.GetAddressOf()))) {
					return false;
				}
				D3D12_SHADER_DESC sd = {};
				reflection->GetDesc(&sd);

				GfxShader* shader = new GfxShader();
				shader->stage = stage;
				shader->id = NextStateId();
				shader->bytecode.assign((const uint8_t*)bytecode, (const uint8_t*)bytecode + size);

				for (UINT r = 0; r < sd.BoundResources; ++r) {
					D3D12_SHADER_INPUT_BIND_DESC bd = {};
					reflection->GetResourceBindingDesc(r, &bd);
					const UINT first = bd.BindPoint;
					const UINT last = bd.BindPoint + (std::max<UINT>)(bd.BindCount, 1u);
					//A dimension of the SRV kind that D3D12 has no value for (BUFFEREX = 11 collides with ray
					//tracing) is a raw buffer view.
					D3D12_SRV_DIMENSION srv_dim = (bd.Dimension == D3D_SRV_DIMENSION_BUFFEREX || bd.Dimension == D3D_SRV_DIMENSION_BUFFER)
						? D3D12_SRV_DIMENSION_BUFFER : (D3D12_SRV_DIMENSION)bd.Dimension;
					switch (bd.Type) {
					case D3D_SIT_CBUFFER:
						for (UINT i = first; i < last && i < GFX_CBV_SLOTS; ++i) {
							shader->cbv_used.set(i);
							shader->cbv_count = (std::max)(shader->cbv_count, i + 1);
						}
						break;
					case D3D_SIT_TEXTURE:
					case D3D_SIT_TBUFFER:
					case D3D_SIT_STRUCTURED:
					case D3D_SIT_BYTEADDRESS:
						for (UINT i = first; i < last && i < GFX_SRV_SLOTS; ++i) {
							shader->srv_used.set(i);
							shader->srv_dimension[i] = srv_dim;
							shader->srv_count = (std::max)(shader->srv_count, i + 1);
						}
						break;
					case D3D_SIT_SAMPLER:
						for (UINT i = first; i < last && i < GFX_SAMPLER_SLOTS; ++i) {
							shader->sampler_used.set(i);
							shader->sampler_count = (std::max)(shader->sampler_count, i + 1);
						}
						break;
					case D3D_SIT_UAV_RWTYPED:
					case D3D_SIT_UAV_RWSTRUCTURED:
					case D3D_SIT_UAV_RWBYTEADDRESS:
					case D3D_SIT_UAV_APPEND_STRUCTURED:
					case D3D_SIT_UAV_CONSUME_STRUCTURED:
					case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
						for (UINT i = first; i < last && i < GFX_UAV_SLOTS; ++i) {
							shader->uav_used.set(i);
							shader->uav_dimension[i] = (srv_dim == D3D12_SRV_DIMENSION_BUFFER)
								? D3D12_UAV_DIMENSION_BUFFER : (D3D12_UAV_DIMENSION)bd.Dimension;
							shader->uav_count = (std::max)(shader->uav_count, i + 1);
						}
						break;
					default:
						break;
					}
				}
				if (stage == GfxShader::CS) {
					reflection->GetThreadGroupSize(&shader->thread_group[0], &shader->thread_group[1], &shader->thread_group[2]);
				}
				*out = shader;
				return true;
			}

			//-------------------------------------------------------------------------------------------
			// Debug layer
			//-------------------------------------------------------------------------------------------

			void GfxDevice::DrainDebugMessages() {
				if (!debug_layer || device == nullptr) {
					return;
				}
				ComPtr<ID3D12InfoQueue> info;
				if (FAILED(device.As(&info))) {
					return;
				}
				const UINT64 count = info->GetNumStoredMessages();
				for (UINT64 i = 0; i < count; ++i) {
					SIZE_T length = 0;
					info->GetMessage(i, nullptr, &length);
					std::vector<uint8_t> storage(length);
					D3D12_MESSAGE* message = (D3D12_MESSAGE*)storage.data();
					if (FAILED(info->GetMessage(i, message, &length))) {
						continue;
					}
					switch (message->Severity) {
					case D3D12_MESSAGE_SEVERITY_CORRUPTION:
					case D3D12_MESSAGE_SEVERITY_ERROR:
						++validation_errors;
						LOG_ERROR("D3D12: %s", message->pDescription);
						break;
					case D3D12_MESSAGE_SEVERITY_WARNING:
						LOG_WARN("D3D12: %s", message->pDescription);
						break;
					default:
						break;
					}
				}
				info->ClearStoredMessages();
			}
		}
	}
}
