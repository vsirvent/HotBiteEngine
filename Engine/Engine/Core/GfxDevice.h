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

//The Direct3D 12 objects the engine draws with: resources that know their own state, views, state objects,
//shaders and the device that makes them. See Docs/D3D12_MIGRATION.md.
//
//This is not a second API over D3D12. The objects are thin owners of the D3D12 ones (the descriptors and
//the ID3D12Resource are public) and exist because D3D12 asks for three things D3D11 did behind the
//scenes, which the engine's ~250 call sites relied on without knowing:
//  - a resource has a *state* that must be changed with a barrier before it is used another way, so
//    every resource tracks its own and GfxContext moves it when something is bound;
//  - a resource cannot be freed while the GPU may still read it, so Release() of anything that owns GPU
//    memory defers the destruction until the frames in flight are done;
//  - a view is a descriptor in a heap, not an object the driver keeps, so views are small objects that
//    own one.

#include <winsock2.h>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <atomic>
#include <bitset>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace HotBite {
	namespace Engine {
		namespace Core {

			//Frames the CPU may record ahead of the GPU. Everything that is written by the CPU and read
			//by the GPU later (constants, per-frame uploads, descriptor tables) is ring-allocated over
			//this many frames.
			constexpr UINT GFX_FRAMES_IN_FLIGHT = 3;

			//The binding slots of the D3D11 model the HLSL is written against (t0-t127, b0-b13, s0-s15,
			//u0-u63, 8 render targets). Kept as they were so no shader register changes.
			constexpr UINT GFX_SRV_SLOTS = 128;
			constexpr UINT GFX_CBV_SLOTS = 14;
			constexpr UINT GFX_SAMPLER_SLOTS = 16;
			constexpr UINT GFX_UAV_SLOTS = 64;
			constexpr UINT GFX_RTV_SLOTS = 8;
			//Most textures a compute shader can index from its unbounded array (Texture2D t[] : register(t0, space1), see
			//GfxContext::CSSetBindlessSRVs).
			constexpr UINT GFX_BINDLESS_SLOTS = 1024;

			//Every state a shader can read a resource in. A texture bound as an SRV is moved here whichever
			//stage reads it, which costs nothing (one read state) and spares tracking the stage.
			constexpr D3D12_RESOURCE_STATES GFX_STATE_SHADER_READ =
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			//What a buffer that only the GPU reads (vertex/index data, BVH, ...) lives in for good. A buffer
			//can be in several read states at once, so this one state serves as vertex buffer, index buffer,
			//SRV and indirect argument and never needs a barrier.
			constexpr D3D12_RESOURCE_STATES GFX_STATE_BUFFER_READ =
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE;

			class GfxDevice;

			//Reference counted, like the COM objects it replaces (`x->Release()` stays valid at every call
			//site). The last Release() of an object that owns GPU memory hands it to the device, which
			//destroys it once the GPU has finished the frames that could have used it.
			class GfxObject {
			public:
				GfxObject() = default;
				GfxObject(const GfxObject&) = delete;
				GfxObject& operator=(const GfxObject&) = delete;
				virtual ~GfxObject() = default;

				void AddRef() { refs.fetch_add(1, std::memory_order_relaxed); }
				//Takes a reference only if the object still has one (false: its last was just dropped and it is on its way out).
				bool TryAddRef() {
					LONG n = refs.load(std::memory_order_relaxed);
					while (n > 0) {
						if (refs.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel)) {
							return true;
						}
					}
					return false;
				}
				LONG Release();
				//Called when the last reference goes, by whoever drops it (Core::LoadTexture's cache uses it to forget the object:
				//the GfxContext holds references to what is bound, so the last one is not always the cache user's).
				void (*on_last_release)(GfxObject*) = nullptr;

			protected:
				//Set by what owns a GPU resource or a descriptor.
				bool gpu_owned = false;

			private:
				std::atomic<LONG> refs{ 1 };
			};

			//-------------------------------------------------------------------------------------------
			// Resources
			//-------------------------------------------------------------------------------------------

			class GfxResource : public GfxObject {
			public:
				Microsoft::WRL::ComPtr<ID3D12Resource> resource;
				D3D12_RESOURCE_DESC desc = {};
				D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;

				ID3D12Resource* Get() const { return resource.Get(); }
				UINT SubresourceCount() const { return subresource_count; }

				//The state of one subresource as the commands recorded so far leave it.
				D3D12_RESOURCE_STATES State(UINT subresource) const {
					return split ? state_sub[subresource] : state_all;
				}
				//Every subresource at once.
				void SetAllState(D3D12_RESOURCE_STATES state) {
					split = false;
					state_all = state;
				}
				//One subresource. The resource splits into per-subresource tracking the first time its
				//subresources differ (a mip chain that is read at one level and written at another).
				void SetSubresourceState(UINT subresource, D3D12_RESOURCE_STATES state) {
					if (!split) {
						if (state == state_all) {
							return;
						}
						split = true;
						state_sub.assign(subresource_count, state_all);
					}
					state_sub[subresource] = state;
				}
				bool IsSplit() const { return split; }
				D3D12_RESOURCE_STATES AllState() const { return state_all; }
				//Goes back to tracking one state when every subresource has come to the same one.
				void MergeIfUniform() {
					if (!split) {
						return;
					}
					for (UINT i = 1; i < state_sub.size(); ++i) {
						if (state_sub[i] != state_sub[0]) {
							return;
						}
					}
					state_all = state_sub[0];
					split = false;
				}

				//Set when a dispatch or draw wrote the resource through a UAV: the next access needs a UAV
				//barrier (or the transition that replaces it).
				bool uav_dirty = false;

				//Set on a resource that lives in one state for good and that GfxContext must never transition: the buffers
				//of a ray tracing acceleration structure (state RAYTRACING_ACCELERATION_STRUCTURE, which no other state can
				//be combined with) and the scratch memory of its builds. Bound as a view they are left alone.
				bool no_transitions = false;

				//Which queue touched the resource last (GfxContext, async compute): the queue that wrote it and the
				//sync fence value of the command list segment that did, and the same for each queue's reads since that
				//write. 0 = nothing recorded. A command on the other queue that conflicts waits for these values.
				uint8_t last_write_queue = 0;
				uint64_t last_write_value = 0;
				uint64_t last_read_value[2] = { 0, 0 };

			protected:
				friend class GfxDevice;
				UINT subresource_count = 1;
				D3D12_RESOURCE_STATES state_all = D3D12_RESOURCE_STATE_COMMON;
				std::vector<D3D12_RESOURCE_STATES> state_sub;
				bool split = false;
			};

			//What a buffer is for, which decides where it lives and how the CPU writes it.
			enum class GfxBufferKind {
				//GPU memory, written by GfxContext::UpdateBuffer (ring + copy) or by shaders.
				Static,
				//A constant buffer. No resource of its own: every UpdateBuffer lands in the frame's upload
				//ring and the buffer remembers where; `shadow` keeps the last contents so a buffer that is
				//used again in a later frame without being rewritten (screenW/screenH, written once) is
				//copied into that frame's ring again instead of pointing at memory that was recycled.
				Constant,
				//Written by the CPU every frame and only read as vertex/index data: ring-backed like a
				//constant buffer but without the shadow (it is rewritten before every use).
				Dynamic,
				//A persistently mapped upload-heap buffer.
				Upload,
				//A persistently mapped readback-heap buffer.
				Readback,
			};

			class GfxBuffer : public GfxResource {
			public:
				GfxBufferKind kind = GfxBufferKind::Static;
				UINT64 size = 0;
				UINT stride = 0;

				//Upload and Readback: the persistent mapping.
				void* mapped = nullptr;

				//Static: the state the buffer lives in between writes. A buffer that is only read (vertex data, a
				//BVH) is created in GFX_STATE_BUFFER_READ and put back there after UpdateBuffer has copied into it.
				D3D12_RESOURCE_STATES resting_state = D3D12_RESOURCE_STATE_COMMON;

				//Constant and Dynamic: where the latest contents are.
				D3D12_GPU_VIRTUAL_ADDRESS ring_va = 0;
				ID3D12Resource* ring_resource = nullptr;
				UINT64 ring_offset = 0;
				//The frame serial the ring contents belong to; ~0 = never written.
				uint64_t ring_frame = ~0ull;
				std::vector<uint8_t> shadow;

				//Readback: the fence value after which the copy that fills it can be read (see
				//GfxContext::FrameFence).
				uint64_t ready_fence = 0;

				D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const {
					return (kind == GfxBufferKind::Constant || kind == GfxBufferKind::Dynamic)
						? ring_va
						: (resource ? resource->GetGPUVirtualAddress() : 0);
				}
			};

			class GfxTexture : public GfxResource {
			public:
				UINT width = 0;
				UINT height = 0;
				UINT depth_or_array = 1;
				UINT mip_levels = 1;
				DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
				//Set on a texture that is one of several placed over the same heap (CreateAliasedTextures2D): the heap
				//lives as long as any of them does. Only one of the group may be used at a time.
				Microsoft::WRL::ComPtr<ID3D12Heap> heap;
			};

			//One surface of an initial-data upload (a mip of an array slice).
			struct GfxSubresourceData {
				const void* data = nullptr;
				UINT row_pitch = 0;
				UINT slice_pitch = 0;
			};

			//-------------------------------------------------------------------------------------------
			// Views
			//-------------------------------------------------------------------------------------------

			//A descriptor over (part of) a resource. The subresources it covers are what GfxContext moves
			//to the right state when the view is bound.
			class GfxView : public GfxObject {
			public:
				~GfxView() override;

				GfxResource* Resource() const { return resource; }
				D3D12_CPU_DESCRIPTOR_HANDLE Cpu() const { return cpu; }
				DXGI_FORMAT Format() const { return format; }

				//The mips and array slices it covers; for a buffer, subresource 0.
				UINT first_mip = 0;
				UINT mip_count = 1;
				UINT first_slice = 0;
				UINT slice_count = 1;
				bool covers_all = true;

				//Calls f(subresource) for each one the view covers.
				template<class F>
				void ForEachSubresource(F&& f) const;

			protected:
				friend class GfxDevice;
				GfxResource* resource = nullptr;
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
				UINT pool_index = ~0u;
				D3D12_DESCRIPTOR_HEAP_TYPE pool_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
				UINT mip_levels_total = 1;
				UINT array_size_total = 1;
			};

			template<class F>
			void GfxView::ForEachSubresource(F&& f) const {
				if (resource == nullptr) {
					return;
				}
				for (UINT s = 0; s < slice_count; ++s) {
					for (UINT m = 0; m < mip_count; ++m) {
						f((first_slice + s) * mip_levels_total + first_mip + m);
					}
				}
			}

			class GfxSRV : public GfxView {
			public:
				D3D12_SRV_DIMENSION dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			};

			class GfxUAV : public GfxView {
			public:
				D3D12_UAV_DIMENSION dimension = D3D12_UAV_DIMENSION_BUFFER;
			};

			class GfxRTV : public GfxView {
			};

			class GfxDSV : public GfxView {
			public:
				bool read_only = false;
			};

			//-------------------------------------------------------------------------------------------
			// State objects
			//-------------------------------------------------------------------------------------------

			class GfxSampler : public GfxObject {
			public:
				GfxSampler();
				~GfxSampler() override;
				D3D12_SAMPLER_DESC desc = {};
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
				//Never reused, so a table cache keyed by it cannot be fooled by a new sampler at an old address.
				uint64_t id = 0;
			private:
				friend class GfxDevice;
				UINT pool_index = ~0u;
			};

			class GfxRasterizerState : public GfxObject {
			public:
				D3D12_RASTERIZER_DESC desc = {};
				//D3D12 always clips to a scissor rectangle; this says whether the state wants the one the
				//app set (D3D11's ScissorEnable) or none.
				bool scissor_enable = false;
				uint64_t id = 0;
			};

			class GfxBlendState : public GfxObject {
			public:
				D3D12_BLEND_DESC desc = {};
				uint64_t id = 0;
			};

			class GfxDepthStencilState : public GfxObject {
			public:
				D3D12_DEPTH_STENCIL_DESC desc = {};
				uint64_t id = 0;
			};

			class GfxInputLayout : public GfxObject {
			public:
				std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
				//The semantic names the elements point into.
				std::vector<std::string> names;
				uint64_t id = 0;
			};

			//-------------------------------------------------------------------------------------------
			// Shaders
			//-------------------------------------------------------------------------------------------

			//Compiled bytecode plus what reflection said it reads, so the context only moves, copies and
			//binds the slots a draw can actually touch.
			class GfxShader : public GfxObject {
			public:
				enum Stage { VS = 0, HS, DS, GS, PS, CS, STAGE_COUNT };

				Stage stage = VS;
				std::vector<uint8_t> bytecode;
				uint64_t id = 0;

				std::bitset<GFX_SRV_SLOTS> srv_used;
				//The dimension each used SRV register is read as, so an unbound slot gets a null descriptor of
				//that kind (D3D11 read zeros from an unbound slot of any kind).
				D3D12_SRV_DIMENSION srv_dimension[GFX_SRV_SLOTS] = {};
				std::bitset<GFX_CBV_SLOTS> cbv_used;
				std::bitset<GFX_SAMPLER_SLOTS> sampler_used;
				std::bitset<GFX_UAV_SLOTS> uav_used;
				D3D12_UAV_DIMENSION uav_dimension[GFX_UAV_SLOTS] = {};
				//Highest used register + 1 of each kind: the length of the descriptor table to build.
				UINT srv_count = 0;
				UINT cbv_count = 0;
				UINT sampler_count = 0;
				UINT uav_count = 0;
				//The shader indexes an unbounded array of textures (register space 1: the compute root signature's bindless table).
				bool bindless = false;
				//Compute only.
				UINT thread_group[3] = { 1, 1, 1 };

				D3D12_SHADER_BYTECODE Bytecode() const {
					return { bytecode.data(), bytecode.size() };
				}
			};

			//-------------------------------------------------------------------------------------------
						// Queries
						//-------------------------------------------------------------------------------------------
			
						//A heap of queries of one kind (timestamps, pipeline statistics) and the readback buffer their results are
						//resolved into. D3D12 has no query objects to poll: the results are copied out by a resolve recorded in the
						//frame, and are readable once the frame that recorded it has finished.
						class GfxQueryHeap : public GfxObject {
						public:
							GfxQueryHeap() { gpu_owned = true; }
							~GfxQueryHeap() override;
			
							Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
							D3D12_QUERY_TYPE query_type = D3D12_QUERY_TYPE_TIMESTAMP;
							UINT count = 0;
							UINT bytes_per_query = 8;
							GfxBuffer* readback = nullptr;
							//The frame fence after which the last resolve can be read; 0 = never resolved.
							uint64_t ready_fence = 0;
			
							//True once the last resolve has landed: its data is in `readback->mapped`.
							bool Ready() const;
							const void* Data() const { return readback ? readback->mapped : nullptr; }
						};
			
						//-------------------------------------------------------------------------------------------
						// Descriptor storage
						//-------------------------------------------------------------------------------------------

			//A CPU-side (not shader visible) heap of descriptors with a free list. Views take one for
			//their whole life; the context copies from here into the shader-visible rings at draw time.
			class GfxDescriptorPool {
			public:
				bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity);
				bool Alloc(D3D12_CPU_DESCRIPTOR_HANDLE& handle, UINT& index);
				void Free(UINT index);
				D3D12_CPU_DESCRIPTOR_HANDLE Handle(UINT index) const {
					return { base.ptr + (SIZE_T)index * increment };
				}
				UINT Increment() const { return increment; }

			private:
				Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
				D3D12_CPU_DESCRIPTOR_HANDLE base = {};
				UINT increment = 0;
				UINT capacity = 0;
				UINT next = 0;
				std::vector<UINT> free_list;
				std::mutex mutex;
			};

			//-------------------------------------------------------------------------------------------
			// The device
			//-------------------------------------------------------------------------------------------

			class GfxDevice {
			public:
				static GfxDevice* Get() { return instance; }

				GfxDevice() = default;
				~GfxDevice();

				//Creates the device on the adapter with the most dedicated memory. Requires feature level
				//12_0 and resource binding tier 2 (descriptor tables may be partly unpopulated).
				HRESULT Init();
				//Waits for the GPU, destroys what was deferred. After this the device must not be used.
				void Shutdown();

				ID3D12Device* Device() const { return device.Get(); }
				ID3D12CommandQueue* Queue() const { return queue.Get(); }
				ID3D12CommandQueue* ComputeQueue() const { return compute_queue.Get(); }
				IDXGIFactory4* Factory() const { return factory.Get(); }
				IDXGIAdapter1* Adapter() const { return adapter.Get(); }
				D3D_FEATURE_LEVEL FeatureLevel() const { return feature_level; }
				bool AllowTearing() const { return allow_tearing; }
				const std::string& AdapterName() const { return adapter_name; }

				//---- resources --------------------------------------------------------------------

				//`initial_data` (size bytes) fills the buffer. A Static buffer is created in COPY_DEST, filled
				//by an immediate upload and left in `resting_state`; a buffer with no data is created in
				//`resting_state`. Constant and Dynamic buffers have no resource of their own.
				HRESULT CreateBuffer(GfxBufferKind kind, UINT64 size, UINT stride, D3D12_RESOURCE_FLAGS flags,
					const void* initial_data, D3D12_RESOURCE_STATES resting_state, GfxBuffer** out);

				//---- ray tracing --------------------------------------------------------------------
				//
				//Inline ray queries (DXR 1.1 and shader model 6.5): a compute shader asks an acceleration structure
				//(RaytracingAccelerationStructure, a descriptor in the SRV table like any other) whether a ray hits
				//something. True when the device has them; everything below is only meaningful then.
				bool RayQuerySupported() const { return ray_query_supported; }
				ID3D12Device5* Device5() const { return device5.Get(); }
				//A buffer for an acceleration structure (`scratch` false: created in the state those live in for good) or for the
				//scratch memory of a build (`scratch` true: a UAV). Neither is ever transitioned by GfxContext (no_transitions).
				//Released like any buffer: the GPU is done with it before it is destroyed.
				HRESULT CreateAccelerationBuffer(UINT64 size, bool scratch, GfxBuffer** out);

				//`data` is one entry per subresource (mip-major within each array slice), or null. A texture
				//with data ends in GFX_STATE_SHADER_READ; one without starts in `initial_state`.
				HRESULT CreateTexture2D(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial_state,
					const D3D12_CLEAR_VALUE* clear_value, const GfxSubresourceData* data, UINT data_count,
					GfxTexture** out);

				//`count` textures of different sizes (but one format and one set of flags) placed at the start of one heap
				//as big as the largest, so they share its memory and cost no more than that one: dynamic resolution keeps
				//a texture per size and switches between them. Their contents are undefined each time one is switched
				//to (GfxContext::SwitchAliased, then clear or overwrite it before reading); they start in COMMON.
				HRESULT CreateAliasedTextures2D(const D3D12_RESOURCE_DESC* descs, UINT count, GfxTexture** out);

				//Wraps a texture somebody else made (a swap chain buffer) so it can be tracked and copied to.
				HRESULT WrapTexture(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, GfxTexture** out);

				//desc may be null: the default view of the whole resource.
				HRESULT CreateSRV(GfxResource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, GfxSRV** out);
				HRESULT CreateUAV(GfxResource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUAV** out);
				HRESULT CreateRTV(GfxResource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc, GfxRTV** out);
				HRESULT CreateDSV(GfxResource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, GfxDSV** out);

								//`type` is D3D12_QUERY_TYPE_TIMESTAMP or D3D12_QUERY_TYPE_PIPELINE_STATISTICS.
				HRESULT CreateQueryHeap(D3D12_QUERY_TYPE type, UINT count, GfxQueryHeap** out);
				//Ticks per second of the timestamps.
				UINT64 TimestampFrequency() const { return timestamp_frequency; }

				//---- state objects ----------------------------------------------------------------

				HRESULT CreateSampler(const D3D12_SAMPLER_DESC& desc, GfxSampler** out);
				HRESULT CreateRasterizerState(const D3D12_RASTERIZER_DESC& desc, bool scissor_enable, GfxRasterizerState** out);
				HRESULT CreateBlendState(const D3D12_BLEND_DESC& desc, GfxBlendState** out);
				HRESULT CreateDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& desc, GfxDepthStencilState** out);
				HRESULT CreateInputLayout(const D3D12_INPUT_ELEMENT_DESC* elements, UINT count, GfxInputLayout** out);
				//Reads the bytecode's reflection into the usage masks. False if it is not valid bytecode.
				bool CreateShader(GfxShader::Stage stage, const void* bytecode, size_t size, GfxShader** out);

				//---- lifetime ---------------------------------------------------------------------

				//Hands an object whose last reference is gone to the deferred queue.
				void Defer(GfxObject* object);
				//Frees what the GPU is done with. Called at the start of every frame.
				void CollectGarbage();

				//Fence values. Every frame is given a serial; the fence reaches it when that frame's commands have
				//executed. FrameSerial() is the serial of the frame being recorded.
				uint64_t FrameSerial() const { return recording_serial.load(); }
				uint64_t CompletedFrame() const { return fence->GetCompletedValue(); }
				bool IsFrameComplete(uint64_t serial) const { return fence->GetCompletedValue() >= serial; }
				//Blocks until the GPU has executed the frame `serial` (and everything before it).
				void WaitFrame(uint64_t serial);
				//How long the CPU spent blocked on the GPU, for the periodic "gfx frame" log line (reset by TakeWaitStats).
				struct WaitStats {
					uint64_t frame_ns = 0;			//WaitFrame: the frame slot being reused, or a Flush
					uint32_t frame_waits = 0;		//how many of those really blocked
					uint64_t immediate_ns = 0;		//ExecuteImmediate waiting for the previous private list
					uint32_t immediate_waits = 0;
					uint32_t immediate_calls = 0;
				};
				WaitStats TakeWaitStats();
				//Submitted by GfxContext when it executes a frame; returns the serial that frame was given.
				uint64_t SubmitFrame(ID3D12CommandList* list);

				//---- the compute queue ----------------------------------------------------------------
				//
				//A second queue, of type COMPUTE, that runs beside the direct one: a dispatch flagged async goes there
				//and overlaps the graphics work of the frame. A frame is a sequence of *segments* - a command list on
				//one of the queues, started after some waits, signalling a value when it is done. The values are the
				//sync fences', one per queue, and only ever grow (also across frames). Every frame's segments are
				//executed by GfxContext::Submit, and the direct queue ends the frame by waiting for the last compute
				//segment, so a frame serial still means that all of the frame is done.
				static constexpr int QUEUE_DIRECT = 0;
				static constexpr int QUEUE_COMPUTE = 1;
				struct QueueWait {
					int queue;			//the queue whose segment is waited for
					uint64_t value;		//the value of its sync fence
				};
				bool HasComputeQueue() const { return compute_queue != nullptr; }
				//Executes one segment: the queue first waits for `waits`, runs `list`, then signals its sync fence with
				//`signal_value`. A compute segment also waits for the immediate uploads made so far (new resources).
				void ExecuteSegment(int queue_index, ID3D12CommandList* list, const QueueWait* waits, UINT wait_count,
					uint64_t signal_value);
				//Ends a frame whose segments were executed: the direct queue waits for `join` (the last compute
				//segment, if there was one), then signals the frame fence. Returns the frame's serial.
				uint64_t FinishFrame(const QueueWait* join);
				std::mutex wait_stats_mutex;
				WaitStats wait_stats;
				//Blocks until the queue is empty.
				void WaitIdle();

				//Runs `record` on a private command list, executes it now (ahead of the frame being
				//recorded) and returns once it has been queued. Thread safe. For creating resources with
				//contents: nothing the frame list already recorded may touch what this writes.
				void TraceCreation(const char* what, int a, UINT64 b, UINT64 c);
				void ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record,
					std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>&& keep_alive);

				//---- descriptors ------------------------------------------------------------------

				GfxDescriptorPool& CbvSrvUavPool() { return pool_cbv_srv_uav; }
				GfxDescriptorPool& SamplerPool() { return pool_sampler; }
				UINT CbvSrvUavIncrement() const { return inc_cbv_srv_uav; }
				UINT SamplerIncrement() const { return inc_sampler; }

				//Null descriptors, one per dimension a shader can read, for unbound slots.
				D3D12_CPU_DESCRIPTOR_HANDLE NullSRV(D3D12_SRV_DIMENSION dimension) const;
				D3D12_CPU_DESCRIPTOR_HANDLE NullUAV(D3D12_UAV_DIMENSION dimension) const;
				//A CBV over a zeroed buffer, for an unbound constant buffer slot.
				D3D12_CPU_DESCRIPTOR_HANDLE NullCBV() const { return null_cbv; }
				D3D12_GPU_VIRTUAL_ADDRESS ZeroBufferAddress() const { return zero_buffer->GetGPUVirtualAddress(); }
				D3D12_CPU_DESCRIPTOR_HANDLE DefaultSampler() const { return null_sampler; }
								//A render target descriptor with nothing behind it, for a hole in the list of targets.
								D3D12_CPU_DESCRIPTOR_HANDLE NullRTV() const { return null_rtv; }

				uint64_t NextStateId() { return ++state_id; }

				//---- debug layer ------------------------------------------------------------------

				//Writes the debug layer's pending messages to the log and counts the errors. No-op without
				//the layer (HOTBITE_D3D12_DEBUG=1 turns it on; GPU based validation is HOTBITE_D3D12_GBV=1).
				void DrainDebugMessages();
				uint64_t ValidationErrors() const { return validation_errors; }
				bool DebugLayerEnabled() const { return debug_layer; }

				//---- PIX --------------------------------------------------------------------------

				//True when PIX's GPU capturer is in the process (HOTBITE_PIX=1, loaded before the device exists, or PIX
				//started the process): PIX can attach and capture, and the frame's passes carry marker names.
				bool PixCapturerLoaded() const { return pix_loaded; }
				//Asks the capturer to save the next `frames` frames (Present to Present) as a GPU capture, `file` a full path ending
				//in .wpix; it returns at once and the file appears when the capture is written, a few seconds later or more
				//(open it in PIX). False with `error` set when the capturer is not loaded or refuses.
				bool PixCaptureNextFrames(const std::filesystem::path& file, uint32_t frames, std::string& error);

			private:
				static GfxDevice* instance;

				Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
				Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
				Microsoft::WRL::ComPtr<ID3D12Device> device;
				Microsoft::WRL::ComPtr<ID3D12Device5> device5;
				bool ray_query_supported = false;
				Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
				Microsoft::WRL::ComPtr<ID3D12CommandQueue> compute_queue;
				Microsoft::WRL::ComPtr<ID3D12Fence> fence;
				//One per queue, signalled by the segments (see ExecuteSegment).
				Microsoft::WRL::ComPtr<ID3D12Fence> sync_fence[2];
				HANDLE fence_event = nullptr;
				//The serial the frame being recorded will be signalled with.
				std::atomic<uint64_t> recording_serial{ 1 };
				std::mutex queue_mutex;
				D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_12_0;
				bool allow_tearing = false;
				bool debug_layer = false;
				bool pix_loaded = false;
								uint64_t validation_errors = 0;
								UINT64 timestamp_frequency = 1;
								std::string adapter_name;

				GfxDescriptorPool pool_cbv_srv_uav;
				GfxDescriptorPool pool_rtv;
				GfxDescriptorPool pool_dsv;
				GfxDescriptorPool pool_sampler;
				UINT inc_cbv_srv_uav = 0;
				UINT inc_sampler = 0;
				friend class GfxView;
				friend class GfxSampler;

				//Null descriptors.
				Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> null_heap;
				D3D12_CPU_DESCRIPTOR_HANDLE null_srv[16] = {};
				D3D12_CPU_DESCRIPTOR_HANDLE null_uav[8] = {};
				D3D12_CPU_DESCRIPTOR_HANDLE null_cbv = {};
				Microsoft::WRL::ComPtr<ID3D12Resource> zero_buffer;
				Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> null_sampler_heap;
				D3D12_CPU_DESCRIPTOR_HANDLE null_sampler = {};
				Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> null_rtv_heap;
								D3D12_CPU_DESCRIPTOR_HANDLE null_rtv = {};

				struct Deferred {
					uint64_t serial;
					GfxObject* object;
				};
				std::mutex defer_mutex;
				std::deque<Deferred> deferred;

				//Immediate uploads.
				std::mutex immediate_mutex;
				//A ring of private lists: a list can only be reused once the queue has run it, and it runs behind whatever frames are
				//already queued, so with one list every second upload in a row would wait for the GPU to drain.
				static constexpr UINT IMMEDIATE_LISTS = 16;
				Microsoft::WRL::ComPtr<ID3D12CommandAllocator> immediate_allocators[IMMEDIATE_LISTS];
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> immediate_lists[IMMEDIATE_LISTS];
				uint64_t immediate_list_value[IMMEDIATE_LISTS] = {};	//the fence value each list was last submitted with
				UINT immediate_next = 0;
				Microsoft::WRL::ComPtr<ID3D12Fence> immediate_fence;
				uint64_t immediate_value = 0;
				struct PendingUpload {
					uint64_t value;
					std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources;
					uint64_t bytes = 0;
				};
				std::deque<PendingUpload> pending_uploads;
				//What the staging resources of pending_uploads add up to. They are released once the GPU has copied them, which
				//CollectGarbage notices once a frame; a load that creates many textures between two frames would otherwise hold
				//every staging buffer of all of them (as much memory again as the textures), so past this ExecuteImmediate waits.
				uint64_t pending_upload_bytes = 0;
				static constexpr uint64_t MAX_PENDING_UPLOAD_BYTES = 256ull * 1024 * 1024;

				std::atomic<uint64_t> state_id{ 0 };

				void CreateNullDescriptors();
				//Fills the subresource ranges of a view from its description.
				static void SetViewRange(GfxView* view, GfxResource* resource, UINT first_mip, UINT mip_count,
					UINT first_slice, UINT slice_count);
			};
		}
	}
}

//The renderer's objects are used from every namespace of the engine and of the games, the way the D3D11 interfaces
//(which were global) were. Naming them here keeps it that way.
using HotBite::Engine::Core::GfxObject;
using HotBite::Engine::Core::GfxResource;
using HotBite::Engine::Core::GfxBuffer;
using HotBite::Engine::Core::GfxBufferKind;
using HotBite::Engine::Core::GfxTexture;
using HotBite::Engine::Core::GfxSubresourceData;
using HotBite::Engine::Core::GfxView;
using HotBite::Engine::Core::GfxSRV;
using HotBite::Engine::Core::GfxUAV;
using HotBite::Engine::Core::GfxRTV;
using HotBite::Engine::Core::GfxDSV;
using HotBite::Engine::Core::GfxSampler;
using HotBite::Engine::Core::GfxRasterizerState;
using HotBite::Engine::Core::GfxBlendState;
using HotBite::Engine::Core::GfxDepthStencilState;
using HotBite::Engine::Core::GfxInputLayout;
using HotBite::Engine::Core::GfxShader;
using HotBite::Engine::Core::GfxQueryHeap;
using HotBite::Engine::Core::GfxDevice;
using HotBite::Engine::Core::GfxDescriptorPool;
using HotBite::Engine::Core::GFX_FRAMES_IN_FLIGHT;
using HotBite::Engine::Core::GFX_SRV_SLOTS;
using HotBite::Engine::Core::GFX_CBV_SLOTS;
using HotBite::Engine::Core::GFX_SAMPLER_SLOTS;
using HotBite::Engine::Core::GFX_UAV_SLOTS;
using HotBite::Engine::Core::GFX_RTV_SLOTS;
using HotBite::Engine::Core::GFX_BINDLESS_SLOTS;
using HotBite::Engine::Core::GFX_STATE_SHADER_READ;
using HotBite::Engine::Core::GFX_STATE_BUFFER_READ;
