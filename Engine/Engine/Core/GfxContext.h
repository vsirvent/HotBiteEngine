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

#include "GfxDevice.h"

#include <array>
#include <map>

namespace HotBite {
	namespace Engine {
		namespace Core {

			//Holds a reference to a GfxObject the way a D3D11 context held one to what was bound to it, so
			//releasing a shader or a view while it is still bound (a hot reload, a texture that is replaced)
			//cannot leave the context pointing at freed memory.
			template<class T>
			class GfxRef {
			public:
				GfxRef() = default;
				GfxRef(T* p) : ptr(p) { if (ptr) { ptr->AddRef(); } }
				GfxRef(const GfxRef& o) : ptr(o.ptr) { if (ptr) { ptr->AddRef(); } }
				GfxRef(GfxRef&& o) noexcept : ptr(o.ptr) { o.ptr = nullptr; }
				~GfxRef() { if (ptr) { ptr->Release(); } }
				GfxRef& operator=(T* p) {
					if (p) { p->AddRef(); }
					if (ptr) { ptr->Release(); }
					ptr = p;
					return *this;
				}
				GfxRef& operator=(const GfxRef& o) { return *this = o.ptr; }
				T* Get() const { return ptr; }
				T* operator->() const { return ptr; }
				operator T* () const { return ptr; }
			private:
				T* ptr = nullptr;
			};

			//The immediate-context-shaped recorder over one ID3D12GraphicsCommandList.
			//
			//D3D12 has no immediate context, and the engine was written against one: it sets state
			//(shaders, targets, views, buffers), changes some of it between draws and calls Draw/Dispatch,
			//relying on the context to remember the rest, to keep resources legal for what they are bound to,
			//and to execute in order. This class keeps those call names and that behaviour, and does the
			//D3D12 work lazily at Draw/Dispatch from the current state:
			//  - the pipeline state object for (shaders, rasterizer/blend/depth, input layout, topology
			//    type, target formats) is looked up in a cache and created on a miss;
			//  - the slots a bound shader reads are copied into shader-visible descriptor tables;
			//  - every resource those slots, the targets and the vertex/index buffers refer to is moved to
			//    the state its use needs, with one batched barrier;
			//  - a resource written through a UAV gets a UAV barrier before its next use.
			//There is one command list per frame, opened by the first command after a submit and executed by
			//Submit(), so the engine's frame structure (ClearScreen ... Present) needs no changes.
			//
			//It is used from the render thread only. Other threads create resources through GfxDevice.
			class GfxContext {
			public:
				GfxContext() = default;
				~GfxContext();

				bool Init(GfxDevice* device);
				void Shutdown();

				GfxDevice* Device() const { return gfx; }

				//---- frames -----------------------------------------------------------------------

				//The list the commands go into; opens one if none is open.
				ID3D12GraphicsCommandList* List();
				//Executes what has been recorded. Returns the serial of that frame (what the fence reaches
				//when it has run), or 0 if nothing was recorded.
				uint64_t Submit();
				//Submit() and wait for the GPU to finish it.
				void Flush();
				//The serial the commands recorded now will be executed under. A readback copied now can be
				//read once GfxDevice::IsFrameComplete(this) says so.
				uint64_t FrameFence() const { return gfx->FrameSerial(); }

				//---- input assembler ----------------------------------------------------------------

				void IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY topology);
				void IASetInputLayout(GfxInputLayout* layout);
				void IASetVertexBuffers(UINT start_slot, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets);
				void IASetIndexBuffer(GfxBuffer* buffer, DXGI_FORMAT format, UINT offset);
				void IAGetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY* topology) const { *topology = topology_state; }
				void IAGetInputLayout(GfxInputLayout** layout) const;

				//---- shaders and their resources ----------------------------------------------------

				void VSSetShader(GfxShader* shader) { SetShader(GfxShader::VS, shader); }
				void HSSetShader(GfxShader* shader) { SetShader(GfxShader::HS, shader); }
				void DSSetShader(GfxShader* shader) { SetShader(GfxShader::DS, shader); }
				void GSSetShader(GfxShader* shader) { SetShader(GfxShader::GS, shader); }
				void PSSetShader(GfxShader* shader) { SetShader(GfxShader::PS, shader); }
				void CSSetShader(GfxShader* shader) { SetShader(GfxShader::CS, shader); }

				void VSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::VS, start, count, views); }
				void HSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::HS, start, count, views); }
				void DSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::DS, start, count, views); }
				void GSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::GS, start, count, views); }
				void PSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::PS, start, count, views); }
				void CSSetShaderResources(UINT start, UINT count, GfxSRV* const* views) { SetSRVs(GfxShader::CS, start, count, views); }
				//The textures a compute shader indexes by number from an unbounded array (Texture2D t[] : register(t0, space1) with
				//NonUniformResourceIndex): element i of `views` is t[i]. Replaces the previous list; a null is a texture that reads zero.
				//Its own descriptor table, so it costs none of the 128 SRV slots - and no shader has to pick a texture with a switch.
				void CSSetBindlessSRVs(UINT count, GfxSRV* const* views);

				void VSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::VS, start, count, buffers); }
				void HSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::HS, start, count, buffers); }
				void DSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::DS, start, count, buffers); }
				void GSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::GS, start, count, buffers); }
				void PSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::PS, start, count, buffers); }
				void CSSetConstantBuffers(UINT start, UINT count, GfxBuffer* const* buffers) { SetCBs(GfxShader::CS, start, count, buffers); }

				void VSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::VS, start, count, samplers); }
				void HSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::HS, start, count, samplers); }
				void DSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::DS, start, count, samplers); }
				void GSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::GS, start, count, samplers); }
				void PSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::PS, start, count, samplers); }
				void CSSetSamplers(UINT start, UINT count, GfxSampler* const* samplers) { SetSamplers(GfxShader::CS, start, count, samplers); }

				void CSSetUnorderedAccessViews(UINT start, UINT count, GfxUAV* const* views);

				GfxShader* VSGetShader() const { return stages[GfxShader::VS].shader; }
				GfxShader* PSGetShader() const { return stages[GfxShader::PS].shader; }
				GfxShader* GSGetShader() const { return stages[GfxShader::GS].shader; }
				GfxSRV* PSGetShaderResource(UINT slot) const { return slot < GFX_SRV_SLOTS ? stages[GfxShader::PS].srv[slot].Get() : nullptr; }
				GfxSampler* PSGetSampler(UINT slot) const { return slot < GFX_SAMPLER_SLOTS ? stages[GfxShader::PS].sampler[slot].Get() : nullptr; }
				GfxBuffer* VSGetConstantBuffer(UINT slot) const { return slot < GFX_CBV_SLOTS ? stages[GfxShader::VS].cb[slot].Get() : nullptr; }

				//---- output merger and rasterizer ---------------------------------------------------

				void OMSetRenderTargets(UINT count, GfxRTV* const* rtvs, GfxDSV* dsv);
				void OMGetRenderTargets(UINT count, GfxRTV** rtvs, GfxDSV** dsv) const;
				void OMSetBlendState(GfxBlendState* state, const float blend_factor[4], UINT sample_mask);
				void OMGetBlendState(GfxBlendState** state, float blend_factor[4], UINT* sample_mask) const;
				void OMSetDepthStencilState(GfxDepthStencilState* state, UINT stencil_ref);
				void OMGetDepthStencilState(GfxDepthStencilState** state, UINT* stencil_ref) const;
				void RSSetState(GfxRasterizerState* state);
				void RSGetState(GfxRasterizerState** state) const;
				void RSSetViewports(UINT count, const D3D12_VIEWPORT* viewports);
				void RSGetViewports(UINT* count, D3D12_VIEWPORT* viewports) const;
				void RSSetScissorRects(UINT count, const D3D12_RECT* rects);
				void RSGetScissorRects(UINT* count, D3D12_RECT* rects) const;
				//Stream output is not used by the engine; kept so callers that unbind it need no change.
				void SOSetTargets(UINT, GfxBuffer* const*, const UINT*) {}

				//---- drawing ----------------------------------------------------------------------

				void Draw(UINT vertex_count, UINT start_vertex);
				void DrawInstanced(UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance);
				void DrawIndexed(UINT index_count, UINT start_index, INT base_vertex);
				void DrawIndexedInstanced(UINT index_count, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance);
				//`async` asks for the compute queue: the dispatch runs beside the graphics work that follows it in the frame,
				//and whatever it reads or writes is synchronised with the rest of the frame by the resources' own tracking
				//(see "Async compute" below), so any dispatch may be flagged. It has no effect while async compute is off
				//or the device has no compute queue: the dispatch is then recorded in order, as if it were not flagged.
				void Dispatch(UINT x, UINT y, UINT z, bool async = false);
				void DispatchIndirect(GfxBuffer* args, UINT offset, bool async = false);

				//---- async compute --------------------------------------------------------------------
				//
				//A frame is recorded as *segments*: a command list on the direct or the compute queue that starts after
				//the waits it was given and signals a value of its queue's sync fence when it ends. Commands are recorded
				//in the order the engine issues them, as before; what changes is that a flagged dispatch goes to the
				//compute list, and that every use of a resource (UseSubresource) is checked against the other queue's:
				//
				//  - the queue that wrote it last, and the queues that read it since, are remembered per resource;
				//  - a use on the other queue that conflicts (a read after a write, a write after anything, and every state
				//    transition, which counts as a write) makes its queue wait for the segment of that earlier use, which
				//    closes that segment and starts a new one after the wait;
				//  - a compute list can only change a resource between compute states (UAV, non-pixel shader resource,
				//    copy, indirect argument): a transition from or to anything else is recorded on the direct list, ahead
				//    of the point the compute queue is made to wait for.
				//
				//Submit() executes the segments in the order they were closed, and the direct queue ends the frame
				//waiting for the last compute segment, so a frame serial still means the whole frame is done.
				//Off, nothing is ever recorded on the compute queue and a frame is one direct segment.
				void SetAsyncCompute(bool enabled);
				bool AsyncCompute() const { return async_enabled && gfx != nullptr && gfx->HasComputeQueue(); }

				//---- clears and copies --------------------------------------------------------------

				void ClearRenderTargetView(GfxRTV* rtv, const float color[4]);
				void ClearDepthStencilView(GfxDSV* dsv, UINT clear_flags, float depth, UINT8 stencil);
				void ClearUnorderedAccessViewUint(GfxUAV* uav, const UINT values[4]);
				void ClearUnorderedAccessViewFloat(GfxUAV* uav, const float values[4]);
				void CopyResource(GfxResource* destination, GfxResource* source);
				void CopyBufferRegion(GfxBuffer* destination, UINT64 destination_offset, GfxBuffer* source, UINT64 source_offset, UINT64 bytes);

				//Writes `bytes` of `data` at `offset` into a buffer. A Constant or Dynamic buffer gets a new
				//place in the frame's upload ring (so draws recorded earlier still see what they saw); a Static
				//one is filled by a copy from the ring; an Upload one is written in place.
				void UpdateBuffer(GfxBuffer* buffer, const void* data, UINT64 bytes, UINT64 offset = 0);

				//---- queries ----------------------------------------------------------------------

				//Writes `data` into one subresource of a texture (rows `row_pitch` bytes apart), through the ring and a copy.
				void UpdateTexture(GfxTexture* texture, UINT subresource, const void* data, UINT row_pitch);
				//Copies subresource 0 of a texture into a Readback buffer (as many bytes as GetCopyableFootprints says; the
				//rows are RowPitch apart, see GfxContext::TextureFootprint). Read the buffer after Flush().
				void CopyTextureToBuffer(GfxTexture* texture, GfxBuffer* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

				//Records the barriers that make `view` usable as `state` (they are written when the next draw, copy or
				//BeginExternal flushes). For code that reads a resource through its own descriptors.
				void TransitionView(const GfxView* view, D3D12_RESOURCE_STATES state) { EnsureOpen(); UseView(view, state); }

				//---- handing the list to somebody else ----------------------------------------------------
				//
				//Code that records straight into the command list (the ImGui backend) needs it with the pending barriers
				//written and nothing of ours half-set. BeginExternal returns the list; the caller may set its own
				//descriptor heaps, root signature, pipeline and targets. EndExternal puts our heaps back and forgets what
				//we had bound, so the next draw sets everything again.
				ID3D12GraphicsCommandList* BeginExternal();
				void EndExternal();

				//A named point in the direct queue's commands, for PIX (its event list and timeline). Does nothing unless PIX's
				//capturer is in the process (GfxDevice::PixCapturerLoaded, HOTBITE_PIX=1).
				void Marker(const char* name);

				void BeginQuery(GfxQueryHeap* heap, UINT index);
				void EndQuery(GfxQueryHeap* heap, UINT index);
				//Copies `count` results from `first` into the heap's readback buffer; they can be read when heap->Ready().
				void ResolveQueryData(GfxQueryHeap* heap, UINT first, UINT count);

				//Moves a resource to a state now, outside of a bind (a texture going to PRESENT, a copy).
				void Transition(GfxResource* resource, D3D12_RESOURCE_STATES state);

				//Makes `value` the contents of a (Static) buffer at `offset` after `bytes`, for upload from
				//the ring: returns the ring chunk so the caller can fill it. Used by UpdateBuffer.
				struct RingAllocation {
					void* cpu = nullptr;
					D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
					ID3D12Resource* resource = nullptr;
					UINT64 offset = 0;
				};
				RingAllocation AllocateUpload(UINT64 bytes, UINT64 alignment = 256);

				//---- queries ----------------------------------------------------------------------

				//Counters for the frame (reset by the owner when it reads them).
				struct Stats {
					uint32_t draws = 0;
					uint32_t dispatches = 0;
					uint32_t pipelines_created = 0;
					uint32_t barriers = 0;
					uint32_t descriptors_copied = 0;
										//Draws and dispatches that were asked for and not recorded (no shader, a pipeline that did not
										//build). Each is also logged, the first few times.
										uint32_t skipped = 0;
									};
				Stats stats;
				size_t PipelineCount() const { return graphics_pipelines.size() + compute_pipelines.size(); }

				//Async compute counters of the frame being recorded; the engine reads them through RenderSystem.
				struct AsyncStats {
					uint32_t dispatches = 0;		//dispatches recorded on the compute queue
					uint32_t segments = 0;			//command lists the frame was split into
					uint32_t waits = 0;				//waits one queue was made to do for the other
				};
				AsyncStats async_stats;

			private:
				struct StageState {
					GfxRef<GfxShader> shader;
					GfxRef<GfxSRV> srv[GFX_SRV_SLOTS];
					GfxRef<GfxBuffer> cb[GFX_CBV_SLOTS];
					GfxRef<GfxSampler> sampler[GFX_SAMPLER_SLOTS];
					GfxRef<GfxUAV> uav[GFX_UAV_SLOTS];
					//The tables this stage last bound are stale (a slot or the shader changed).
					bool srv_dirty = true;
					bool cb_dirty = true;
					bool sampler_dirty = true;
					bool uav_dirty = true;
					//What the bound constant buffer table was built from, to notice a buffer that was rewritten.
					D3D12_GPU_VIRTUAL_ADDRESS cb_bound[GFX_CBV_SLOTS] = {};
					//The list the tables below were set on (compute only: a dispatch may go to either queue's list).
					uint64_t table_epoch = 0;
					//Root descriptor tables currently set on the list.
					D3D12_GPU_DESCRIPTOR_HANDLE srv_table = {};
					D3D12_GPU_DESCRIPTOR_HANDLE cb_table = {};
					D3D12_GPU_DESCRIPTOR_HANDLE sampler_table = {};
					D3D12_GPU_DESCRIPTOR_HANDLE uav_table = {};
				};

				//The compute stage's bindless table (see CSSetBindlessSRVs).
				GfxRef<GfxSRV> cs_bindless[GFX_BINDLESS_SLOTS];
				UINT cs_bindless_count = 0;
				bool cs_bindless_dirty = true;

				//A shader-visible ring of descriptors, split into one region per frame in flight.
				struct DescriptorRing {
					Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
					D3D12_CPU_DESCRIPTOR_HANDLE cpu_base = {};
					D3D12_GPU_DESCRIPTOR_HANDLE gpu_base = {};
					UINT increment = 0;
					UINT region_size = 0;
					UINT head = 0;
					UINT region = 0;
				};

				struct UploadRing {
					Microsoft::WRL::ComPtr<ID3D12Resource> resource;
					uint8_t* cpu = nullptr;
					UINT64 region_size = 0;
					UINT64 head = 0;
					UINT64 region = 0;
				};

				//When a frame needs more upload memory than its region of the ring holds, further allocations come from
				//one-off chunks that live until the GPU has finished the frame (a flush would be unsafe in the middle
				//of resolving a draw).
				struct OverflowChunk {
					Microsoft::WRL::ComPtr<ID3D12Resource> resource;
					uint8_t* cpu = nullptr;
					UINT64 size = 0;
					UINT64 head = 0;
				};
				std::vector<OverflowChunk> overflow[GFX_FRAMES_IN_FLIGHT];

				struct GraphicsKey {
					std::array<uint64_t, 28> v;
					bool operator==(const GraphicsKey& o) const { return v == o.v; }
				};
				struct GraphicsKeyHash {
					size_t operator()(const GraphicsKey& k) const {
						uint64_t h = 1469598103934665603ull;
						for (uint64_t x : k.v) {
							h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
						}
						return (size_t)h;
					}
				};

				GfxDevice* gfx = nullptr;
				//The direct list is open (commands are being recorded into `list`).
				bool open = false;
				//The frame has begun: its slot is free to use and the rings are reset (see BeginFrame).
				bool frame_open = false;

				//The open direct list, one of direct_pool (a frame may use several: a list is closed when the direct queue has
				//to wait for the compute queue, and the commands after the wait go to the next one).
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
				std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> direct_pool;
				UINT direct_used = 0;
				Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[GFX_FRAMES_IN_FLIGHT];

				//Async compute (see the public section): the open compute list, its pool and allocators, and the bookkeeping
				//that decides what each queue waits for.
				//Off unless HOTBITE_ASYNC_COMPUTE=1 or SetAsyncCompute(true): measured, it does not pay on this renderer
				//(see Docs/D3D12_MIGRATION.md, "Async compute").
				bool async_enabled = false;
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> async_list;
				bool async_open = false;
				//Something has been recorded in the open compute list (a wait that would have to precede it cuts the list).
				bool async_work = false;
				std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> async_pool;
				UINT async_used = 0;
				Microsoft::WRL::ComPtr<ID3D12CommandAllocator> async_allocators[GFX_FRAMES_IN_FLIGHT];
				bool async_root_set = false;
				ID3D12PipelineState* async_pipeline = nullptr;
				std::vector<D3D12_RESOURCE_BARRIER> async_barriers;
				//Which queue the commands being resolved belong to (0 direct, 1 compute): where UseSubresource puts barriers.
				int rec_queue = 0;
				//Transitions were recorded on the direct list for the compute dispatch being resolved.
				bool direct_sync_pending = false;
				//The value of the sync fence the open segment of each queue will signal (or the next one, when none is open).
				uint64_t seg_value[2] = { 1, 1 };
				//What the open segment of each queue waits for before it starts.
				std::vector<GfxDevice::QueueWait> seg_waits[2];
				//waited[q][o]: the highest value of queue o's sync fence that the commands recorded on queue q from now on
				//are ordered after.
				uint64_t waited[2][2] = {};
				//Each list that was closed this frame, in the order of closing, executed in that order by Submit.
				struct Segment {
					int queue = 0;
					Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
					std::vector<GfxDevice::QueueWait> waits;
					uint64_t value = 0;
				};
				std::vector<Segment> segments;
				//Lists closed so far, to notice that resolving a command closed the one it was recording into.
				uint64_t cut_counter = 0;
				//Each list that is opened gets a number, so the compute stage knows which list its tables were set on.
				uint64_t epoch_counter = 0;
				uint64_t direct_epoch = 0;
				uint64_t async_epoch = 0;
				//The last compute segment executed this frame: what the direct queue ends the frame waiting for.
				uint64_t compute_last_value = 0;
				//The serial each slot was last submitted under, to wait for before reusing its allocator and rings.
				uint64_t slot_serial[GFX_FRAMES_IN_FLIGHT] = {};
				UINT slot = 0;

				DescriptorRing ring_srv;
				DescriptorRing ring_sampler;
				UploadRing upload;

				Microsoft::WRL::ComPtr<ID3D12RootSignature> graphics_root;
				Microsoft::WRL::ComPtr<ID3D12RootSignature> compute_root;
				Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatch_signature;
				std::unordered_map<GraphicsKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, GraphicsKeyHash> graphics_pipelines;
				std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> compute_pipelines;
				//Sampler tables are small and few; they live in a persistent part of the sampler heap, keyed by
				//the samplers they hold.
				std::map<std::array<uint64_t, GFX_SAMPLER_SLOTS + 1>, D3D12_GPU_DESCRIPTOR_HANDLE> sampler_tables;
				UINT sampler_persistent_head = 0;
				UINT sampler_persistent_size = 0;

				StageState stages[GfxShader::STAGE_COUNT];

				//Fixed-function state.
				D3D_PRIMITIVE_TOPOLOGY topology_state = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				GfxRef<GfxInputLayout> input_layout;
				GfxRef<GfxBuffer> vertex_buffers[16];
				UINT vertex_strides[16] = {};
				UINT vertex_offsets[16] = {};
				GfxRef<GfxBuffer> index_buffer;
				DXGI_FORMAT index_format = DXGI_FORMAT_R32_UINT;
				UINT index_offset = 0;
				GfxRef<GfxRTV> render_targets[GFX_RTV_SLOTS];
				UINT render_target_count = 0;
				GfxRef<GfxDSV> depth_target;
				GfxRef<GfxBlendState> blend_state;
				float blend_factor[4] = { 1, 1, 1, 1 };
				UINT sample_mask = 0xFFFFFFFFu;
				GfxRef<GfxDepthStencilState> depth_state;
				UINT stencil_ref = 0;
				GfxRef<GfxRasterizerState> raster_state;
				D3D12_VIEWPORT viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
				UINT viewport_count = 0;
				D3D12_RECT scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
				UINT scissor_count = 0;

				GfxRef<GfxBlendState> default_blend;
				GfxRef<GfxDepthStencilState> default_depth;
				GfxRef<GfxRasterizerState> default_raster;

				//What is currently set on the open list; anything that differs is re-issued at the next draw.
				bool om_dirty = true;
				bool viewport_dirty = true;
				bool scissor_dirty = true;
				bool topology_dirty = true;
				bool ia_dirty = true;
				bool blend_dirty = true;
				bool stencil_dirty = true;
				bool graphics_root_set = false;
				bool compute_root_set = false;
				ID3D12PipelineState* current_pipeline = nullptr;

				std::vector<D3D12_RESOURCE_BARRIER> barriers;

				bool CreateRootSignatures();
				bool CreateRings();
				//Opens the direct list (and the frame, if it has not begun).
				void EnsureOpen();
				void BeginFrame();
				void OpenDirect();
				void EnsureOpenAsync();
				void CloseDirect();
				void CloseAsync();
				void CloseQueue(int queue) { if (queue == GfxDevice::QUEUE_DIRECT) { CloseDirect(); } else { CloseAsync(); } }
				bool QueueOpen(int queue) const { return queue == GfxDevice::QUEUE_DIRECT ? open : async_open; }
				//The segment of queue q that is open, or opened now.
				void EnsureOpenQueue(int queue);
				void ResetRings();
				void MarkAllDirty();
				void FlushBarriers();
				void FlushAsyncBarriers();

				//Cross-queue ordering. Make(q, o, value): the commands queue q records from now on run after segment `value`
				//of queue o (closing it if it is still open). Touch: a use of a resource on queue q, with the waits it needs;
				//it leaves queue q's segment open.
				void MakeWait(int q, int o, uint64_t value);
				void WaitForAll(int q, int o);
				void Touch(GfxResource* resource, bool write, int q);
				//The hazard bookkeeping alone, for a use that has already been ordered (a transition made on the direct queue
				//for the compute queue's benefit).
				void Register(GfxResource* resource, bool write, int q);
				bool Tracking() const { return gfx->HasComputeQueue(); }
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> NewList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* allocator);

				//Moves everything a draw or a dispatch is going to use to the state it needs (this is where a queue may have
				//to wait). They come before any command of the draw is recorded, because they may close the list.
				void UseStageSRVs(GfxShader::Stage stage);
				bool ResolveGraphicsUses();
				void ResolveComputeUses(GfxBuffer* indirect_args);

				void SetShader(GfxShader::Stage stage, GfxShader* shader);
				void SetSRVs(GfxShader::Stage stage, UINT start, UINT count, GfxSRV* const* views);
				void SetCBs(GfxShader::Stage stage, UINT start, UINT count, GfxBuffer* const* buffers);
				void SetSamplers(GfxShader::Stage stage, UINT start, UINT count, GfxSampler* const* samplers);

				bool PrepareGraphics();
								void ReportSkipped(const char* what);
												//HOTBITE_GFX_TRACE=<frame serial>: one frame's draws and dispatches, with what each had bound, in the log.
												void TraceStage(const char* what, GfxShader::Stage stage);
												uint64_t trace_serial = 0;
												bool tracing = false;
				bool PrepareCompute(bool async, GfxBuffer* indirect_args);
				template<class F>
				void RecordDispatch(bool async, F&& record);
				//Builds the descriptor tables of one stage and sets them on `target` (the open direct list, or the compute one).
				bool ResolveStage(GfxShader::Stage stage, bool compute, ID3D12GraphicsCommandList* target);
				ID3D12PipelineState* GraphicsPipeline();
				ID3D12PipelineState* ComputePipeline();

				bool AllocateDescriptors(DescriptorRing& ring, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
				D3D12_GPU_DESCRIPTOR_HANDLE BuildSamplerTable(StageState& st);
				void RepublishConstantBuffer(GfxBuffer* buffer);

				//State transitions.
				//One use of a resource, `before` being the state it is in now (of a subresource, or of all of them when
				//`subresource` is D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES): the barrier if one is needed, and what the queues
				//have to wait for.
				void UseImpl(GfxResource* resource, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
				void UseSubresource(GfxResource* resource, UINT subresource, D3D12_RESOURCE_STATES state);
				void UseView(const GfxView* view, D3D12_RESOURCE_STATES state);
				void UseResource(GfxResource* resource, D3D12_RESOURCE_STATES state);
				void UnbindConflicting(GfxResource* resource, const GfxView* view, bool compute);
				static bool ViewsOverlap(const GfxView* a, const GfxView* b);
			};
		}
	}
}

using HotBite::Engine::Core::GfxContext;
using HotBite::Engine::Core::GfxRef;
