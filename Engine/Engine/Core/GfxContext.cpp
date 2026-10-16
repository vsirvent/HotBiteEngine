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

#include "GfxContext.h"
#include <chrono>
#include "Log.h"

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {
	//Pipelines are compiled by the driver the first time a draw needs them, on the render thread, with the render lock
	//held: a slow one stalls the frame and everything that wants the lock. Say so in the log.
	void LogPipelineTime(const char* kind, std::chrono::steady_clock::time_point begin) {
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
		if (ms >= 5.0) {
			LOG_DEBUG("GfxContext: %s pipeline compiled in %.1f ms", kind, ms);
		}
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {

			namespace {
				//Everything a resource can be in for reading only. A subresource already in a state that contains the
				//one a use asks for needs no barrier.
				constexpr D3D12_RESOURCE_STATES READ_STATES =
					D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE |
					D3D12_RESOURCE_STATE_DEPTH_READ;

				//Descriptors left between the end of what a table uses and the end of its region, because a table is
				//declared 128 SRVs wide and has to lie inside the heap.
				constexpr UINT TABLE_MARGIN = GFX_SRV_SLOTS;

				UINT64 AlignUp(UINT64 value, UINT64 alignment) {
					return (value + alignment - 1) & ~(alignment - 1);
				}

				bool HasStencil(DXGI_FORMAT format) {
					return format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
				}

				D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(D3D_PRIMITIVE_TOPOLOGY topology) {
					switch (topology) {
					case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
						return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
					case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
					case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
					case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
					case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
						return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
					case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
					case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
					case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
					case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
						return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					default:
						//The 1..32 control point patch lists.
						if (topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST &&
							topology <= D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST) {
							return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
						}
						return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
					}
				}

				D3D12_DESCRIPTOR_RANGE1 Range(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT count) {
					D3D12_DESCRIPTOR_RANGE1 r = {};
					r.RangeType = type;
					r.NumDescriptors = count;
					r.BaseShaderRegister = 0;
					r.RegisterSpace = 0;
					//Slots are rewritten between draws and the textures behind them are written by other passes
					//in the same list, so neither the descriptors nor their data may be assumed static.
					r.Flags = (type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
						? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
						: (D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
					r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
					return r;
				}

				D3D12_ROOT_PARAMETER1 TableParam(const D3D12_DESCRIPTOR_RANGE1* range, D3D12_SHADER_VISIBILITY visibility) {
					D3D12_ROOT_PARAMETER1 p = {};
					p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					p.DescriptorTable.NumDescriptorRanges = 1;
					p.DescriptorTable.pDescriptorRanges = range;
					p.ShaderVisibility = visibility;
					return p;
				}

				ComPtr<ID3D12RootSignature> SerializeRoot(ID3D12Device* device, const D3D12_ROOT_PARAMETER1* params, UINT count,
					D3D12_ROOT_SIGNATURE_FLAGS flags) {
					D3D12_VERSIONED_ROOT_SIGNATURE_DESC vd = {};
					vd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
					vd.Desc_1_1.NumParameters = count;
					vd.Desc_1_1.pParameters = params;
					vd.Desc_1_1.Flags = flags;
					ComPtr<ID3DBlob> blob;
					ComPtr<ID3DBlob> error;
					HRESULT hr = D3D12SerializeVersionedRootSignature(&vd, &blob, &error);
					if (FAILED(hr)) {
						LOG_ERROR("GfxContext: root signature: %s", error ? (const char*)error->GetBufferPointer() : "?");
						return nullptr;
					}
					ComPtr<ID3D12RootSignature> root;
					hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
					if (FAILED(hr)) {
						LOG_ERROR("GfxContext: CreateRootSignature failed (0x%08X)", (unsigned)hr);
						return nullptr;
					}
					return root;
				}

				const D3D12_SHADER_VISIBILITY STAGE_VISIBILITY[GfxShader::STAGE_COUNT] = {
					D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_HULL, D3D12_SHADER_VISIBILITY_DOMAIN,
					D3D12_SHADER_VISIBILITY_GEOMETRY, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_SHADER_VISIBILITY_ALL };

				//Root parameter layout. Graphics: three tables per stage in stage order (CBV, SRV, sampler).
				//Compute: CBV, SRV, UAV, sampler.
				inline UINT GraphicsParam(GfxShader::Stage stage, UINT kind) { return (UINT)stage * 3 + kind; }
				constexpr UINT PARAM_CBV = 0;
				constexpr UINT PARAM_SRV = 1;
				constexpr UINT PARAM_SAMPLER = 2;
				constexpr UINT COMPUTE_CBV = 0;
				constexpr UINT COMPUTE_SRV = 1;
				constexpr UINT COMPUTE_UAV = 2;
				constexpr UINT COMPUTE_SAMPLER = 3;
				constexpr UINT COMPUTE_BINDLESS = 4;
			}

			GfxContext::~GfxContext() {
				Shutdown();
			}

			bool GfxContext::Init(GfxDevice* g) {
				gfx = g;
				char trace_value[32] = {};
				size_t trace_len = 0;
				if (getenv_s(&trace_len, trace_value, sizeof(trace_value), "HOTBITE_GFX_TRACE") == 0 && trace_len > 0) {
					trace_serial = (uint64_t)_strtoui64(trace_value, nullptr, 10);
				}
				ID3D12Device* device = gfx->Device();
				for (UINT i = 0; i < GFX_FRAMES_IN_FLIGHT; ++i) {
					if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])))) {
						return false;
					}
					if (gfx->HasComputeQueue() &&
						FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&async_allocators[i])))) {
						return false;
					}
				}
				direct_pool.push_back(NewList(D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get()));
				if (direct_pool[0] == nullptr) {
					return false;
				}
				//HOTBITE_ASYNC_COMPUTE=1 starts with async compute on (the flag can be flipped at run time too).
				{
					char value[8] = {};
					size_t len = 0;
					if (getenv_s(&len, value, sizeof(value), "HOTBITE_ASYNC_COMPUTE") == 0 && len > 0) {
						async_enabled = value[0] == '1';
					}
				}
				if (!CreateRootSignatures() || !CreateRings()) {
					return false;
				}

				D3D12_INDIRECT_ARGUMENT_DESC arg = {};
				arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
				D3D12_COMMAND_SIGNATURE_DESC csd = {};
				csd.ByteStride = sizeof(UINT) * 3;
				csd.NumArgumentDescs = 1;
				csd.pArgumentDescs = &arg;
				if (FAILED(device->CreateCommandSignature(&csd, nullptr, IID_PPV_ARGS(&dispatch_signature)))) {
					return false;
				}

				//The state a D3D11 context starts with.
				D3D12_RASTERIZER_DESC rd = {};
				rd.FillMode = D3D12_FILL_MODE_SOLID;
				rd.CullMode = D3D12_CULL_MODE_BACK;
				rd.DepthClipEnable = TRUE;
				GfxRasterizerState* raster = nullptr;
				gfx->CreateRasterizerState(rd, false, &raster);
				default_raster = raster;
				raster->Release();

				D3D12_BLEND_DESC bd = {};
				for (UINT i = 0; i < 8; ++i) {
					bd.RenderTarget[i].BlendEnable = FALSE;
					bd.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
					bd.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
					bd.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
					bd.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
					bd.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
					bd.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
					bd.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
					bd.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
				}
				GfxBlendState* blend = nullptr;
				gfx->CreateBlendState(bd, &blend);
				default_blend = blend;
				blend->Release();

				D3D12_DEPTH_STENCIL_DESC dd = {};
				dd.DepthEnable = TRUE;
				dd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
				dd.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
				dd.StencilEnable = FALSE;
				dd.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
				dd.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
				dd.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
				dd.BackFace = dd.FrontFace;
				GfxDepthStencilState* depth = nullptr;
				gfx->CreateDepthStencilState(dd, &depth);
				default_depth = depth;
				depth->Release();

				viewport_count = 0;
				scissor_count = 0;
				return true;
			}

			void GfxContext::Shutdown() {
				if (gfx == nullptr) {
					return;
				}
				Flush();
				for (StageState& st : stages) {
					st = StageState();
				}
				for (auto& v : vertex_buffers) { v = nullptr; }
				for (auto& r : render_targets) { r = nullptr; }
				index_buffer = nullptr;
				depth_target = nullptr;
				input_layout = nullptr;
				blend_state = nullptr;
				depth_state = nullptr;
				raster_state = nullptr;
				default_blend = nullptr;
				default_depth = nullptr;
				default_raster = nullptr;
				graphics_pipelines.clear();
				compute_pipelines.clear();
				sampler_tables.clear();
				list.Reset();
				async_list.Reset();
				direct_pool.clear();
				async_pool.clear();
				segments.clear();
				for (auto& a : allocators) { a.Reset(); }
				for (auto& a : async_allocators) { a.Reset(); }
				ring_srv = DescriptorRing();
				ring_sampler = DescriptorRing();
				upload = UploadRing();
				graphics_root.Reset();
				compute_root.Reset();
				dispatch_signature.Reset();
				gfx = nullptr;
			}

			bool GfxContext::CreateRootSignatures() {
				ID3D12Device* device = gfx->Device();

				//Graphics: for each of VS/HS/DS/GS/PS a table of constant buffers, one of SRVs and one of samplers,
				//each visible to its own stage only. That keeps the D3D11 meaning of a register - VS t0 and PS t0
				//are different bindings - so no HLSL has to change.
				D3D12_DESCRIPTOR_RANGE1 cbv = Range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, GFX_CBV_SLOTS);
				D3D12_DESCRIPTOR_RANGE1 srv = Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GFX_SRV_SLOTS);
				D3D12_DESCRIPTOR_RANGE1 uav = Range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, GFX_UAV_SLOTS);
				D3D12_DESCRIPTOR_RANGE1 smp = Range(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, GFX_SAMPLER_SLOTS);

				D3D12_ROOT_PARAMETER1 gp[5 * 3];
				for (UINT s = 0; s < 5; ++s) {
					gp[s * 3 + PARAM_CBV] = TableParam(&cbv, STAGE_VISIBILITY[s]);
					gp[s * 3 + PARAM_SRV] = TableParam(&srv, STAGE_VISIBILITY[s]);
					gp[s * 3 + PARAM_SAMPLER] = TableParam(&smp, STAGE_VISIBILITY[s]);
				}
				graphics_root = SerializeRoot(device, gp, _countof(gp), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

				//An unbounded array of textures in register space 1, for the shaders that pick a texture by number (the ray tracers' hit
				//shading): resource binding tier 2 allows it, and the device is asked for that tier.
				D3D12_DESCRIPTOR_RANGE1 bindless = Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX);
				bindless.RegisterSpace = 1;
				D3D12_ROOT_PARAMETER1 cp[5];
				cp[COMPUTE_CBV] = TableParam(&cbv, D3D12_SHADER_VISIBILITY_ALL);
				cp[COMPUTE_SRV] = TableParam(&srv, D3D12_SHADER_VISIBILITY_ALL);
				cp[COMPUTE_UAV] = TableParam(&uav, D3D12_SHADER_VISIBILITY_ALL);
				cp[COMPUTE_SAMPLER] = TableParam(&smp, D3D12_SHADER_VISIBILITY_ALL);
				cp[COMPUTE_BINDLESS] = TableParam(&bindless, D3D12_SHADER_VISIBILITY_ALL);
				compute_root = SerializeRoot(device, cp, _countof(cp), D3D12_ROOT_SIGNATURE_FLAG_NONE);

				return graphics_root != nullptr && compute_root != nullptr;
			}

			bool GfxContext::CreateRings() {
				ID3D12Device* device = gfx->Device();

				//Shader-visible descriptors: one region per frame in flight.
				{
					const UINT region = 262144;
					D3D12_DESCRIPTOR_HEAP_DESC d = {};
					d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
					d.NumDescriptors = region * GFX_FRAMES_IN_FLIGHT;
					d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					if (FAILED(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&ring_srv.heap)))) {
						LOG_ERROR("GfxContext: could not create the shader-visible descriptor heap");
						return false;
					}
					ring_srv.cpu_base = ring_srv.heap->GetCPUDescriptorHandleForHeapStart();
					ring_srv.gpu_base = ring_srv.heap->GetGPUDescriptorHandleForHeapStart();
					ring_srv.increment = gfx->CbvSrvUavIncrement();
					ring_srv.region_size = region;
				}
				//Samplers: a persistent heap of tables, see BuildSamplerTable.
				{
					D3D12_DESCRIPTOR_HEAP_DESC d = {};
					d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
					d.NumDescriptors = 2048;
					d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					if (FAILED(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&ring_sampler.heap)))) {
						return false;
					}
					ring_sampler.cpu_base = ring_sampler.heap->GetCPUDescriptorHandleForHeapStart();
					ring_sampler.gpu_base = ring_sampler.heap->GetGPUDescriptorHandleForHeapStart();
					ring_sampler.increment = gfx->SamplerIncrement();
					ring_sampler.region_size = 2048;
					sampler_persistent_size = 2048;
				}
				//Upload memory for constants and per-frame data.
				{
					const UINT64 region = 96ull * 1024 * 1024;
					D3D12_HEAP_PROPERTIES hp = {};
					hp.Type = D3D12_HEAP_TYPE_UPLOAD;
					hp.CreationNodeMask = 1;
					hp.VisibleNodeMask = 1;
					D3D12_RESOURCE_DESC rd = {};
					rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					rd.Width = region * GFX_FRAMES_IN_FLIGHT;
					rd.Height = 1;
					rd.DepthOrArraySize = 1;
					rd.MipLevels = 1;
					rd.SampleDesc.Count = 1;
					rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
					if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
						nullptr, IID_PPV_ARGS(&upload.resource)))) {
						LOG_ERROR("GfxContext: could not create the upload ring");
						return false;
					}
					D3D12_RANGE none = { 0, 0 };
					upload.resource->Map(0, &none, (void**)&upload.cpu);
					upload.region_size = region;
				}
				return true;
			}

			//-------------------------------------------------------------------------------------------
			// Frames
			//-------------------------------------------------------------------------------------------

			void GfxContext::MarkAllDirty() {
				for (StageState& st : stages) {
					st.srv_dirty = st.cb_dirty = st.sampler_dirty = st.uav_dirty = true;
					st.srv_table = st.cb_table = st.sampler_table = st.uav_table = {};
					st.table_epoch = direct_epoch;
					cs_bindless_dirty = true;
					for (UINT i = 0; i < GFX_CBV_SLOTS; ++i) {
						st.cb_bound[i] = 0;
					}
				}
				om_dirty = viewport_dirty = scissor_dirty = topology_dirty = ia_dirty = blend_dirty = stencil_dirty = true;
				graphics_root_set = false;
				compute_root_set = false;
				current_pipeline = nullptr;
			}

			void GfxContext::ResetRings() {
				ring_srv.region = slot;
				ring_srv.head = 0;
				upload.region = slot;
				upload.head = 0;
			}

			ComPtr<ID3D12GraphicsCommandList> GfxContext::NewList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* allocator) {
				ComPtr<ID3D12GraphicsCommandList> l;
				if (FAILED(gfx->Device()->CreateCommandList(0, type, allocator, nullptr, IID_PPV_ARGS(&l)))) {
					return nullptr;
				}
				l->Close();
				return l;
			}

			//The frame begins with its first command: its slot's allocators and rings are reused, so the frame that used the
			//slot last must be done first.
			void GfxContext::BeginFrame() {
				if (frame_open) {
					return;
				}
				gfx->WaitFrame(slot_serial[slot]);
				overflow[slot].clear();
				gfx->CollectGarbage();
				gfx->DrainDebugMessages();
				allocators[slot]->Reset();
				if (async_allocators[slot] != nullptr) {
					async_allocators[slot]->Reset();
				}
				direct_used = 0;
				async_used = 0;
				ResetRings();
				stats = Stats();
				async_stats = AsyncStats();
				segments.clear();
				compute_last_value = 0;
				frame_open = true;
				tracing = trace_serial != 0 && gfx->FrameSerial() >= trace_serial && gfx->FrameSerial() < trace_serial + 3;
			}

			void GfxContext::OpenDirect() {
				if (direct_used == direct_pool.size()) {
					ComPtr<ID3D12GraphicsCommandList> fresh = NewList(D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[slot].Get());
					if (fresh == nullptr) {
						LOG_ERROR("GfxContext: could not create a command list");
						return;
					}
					direct_pool.push_back(fresh);
				}
				list = direct_pool[direct_used++];
				list->Reset(allocators[slot].Get(), nullptr);
				ID3D12DescriptorHeap* heaps[] = { ring_srv.heap.Get(), ring_sampler.heap.Get() };
				list->SetDescriptorHeaps(2, heaps);
				open = true;
				direct_epoch = ++epoch_counter;
				MarkAllDirty();
			}

			void GfxContext::EnsureOpen() {
				if (open) {
					return;
				}
				BeginFrame();
				OpenDirect();
			}

			void GfxContext::EnsureOpenAsync() {
				if (async_open) {
					return;
				}
				BeginFrame();
				if (async_used == async_pool.size()) {
					ComPtr<ID3D12GraphicsCommandList> fresh = NewList(D3D12_COMMAND_LIST_TYPE_COMPUTE, async_allocators[slot].Get());
					if (fresh == nullptr) {
						LOG_ERROR("GfxContext: could not create a compute command list");
						return;
					}
					async_pool.push_back(fresh);
				}
				async_list = async_pool[async_used++];
				async_list->Reset(async_allocators[slot].Get(), nullptr);
				ID3D12DescriptorHeap* heaps[] = { ring_srv.heap.Get(), ring_sampler.heap.Get() };
				async_list->SetDescriptorHeaps(2, heaps);
				async_open = true;
				async_work = false;
				async_root_set = false;
				async_pipeline = nullptr;
				async_epoch = ++epoch_counter;
			}

			void GfxContext::EnsureOpenQueue(int queue) {
				if (queue == GfxDevice::QUEUE_DIRECT) {
					EnsureOpen();
				}
				else {
					EnsureOpenAsync();
				}
			}

			//Closes the open direct list as a segment: the barriers recorded so far are written first, the waits it was
			//given go with it, and its value is signalled when the direct queue has run it.
			void GfxContext::CloseDirect() {
				if (!open) {
					return;
				}
				FlushBarriers();
				list->Close();
				open = false;
				Segment seg;
				seg.queue = GfxDevice::QUEUE_DIRECT;
				seg.list = list;
				seg.waits = std::move(seg_waits[GfxDevice::QUEUE_DIRECT]);
				seg_waits[GfxDevice::QUEUE_DIRECT].clear();
				seg.value = seg_value[GfxDevice::QUEUE_DIRECT]++;
				segments.push_back(std::move(seg));
				++cut_counter;
				++async_stats.segments;
			}

			void GfxContext::CloseAsync() {
				if (!async_open) {
					return;
				}
				FlushAsyncBarriers();
				async_list->Close();
				async_open = false;
				async_work = false;
				Segment seg;
				seg.queue = GfxDevice::QUEUE_COMPUTE;
				seg.list = async_list;
				seg.waits = std::move(seg_waits[GfxDevice::QUEUE_COMPUTE]);
				seg_waits[GfxDevice::QUEUE_COMPUTE].clear();
				seg.value = seg_value[GfxDevice::QUEUE_COMPUTE]++;
				compute_last_value = seg.value;
				segments.push_back(std::move(seg));
				++cut_counter;
				++async_stats.segments;
			}

			ID3D12GraphicsCommandList* GfxContext::List() {
				EnsureOpen();
				return list.Get();
			}

			void GfxContext::FlushBarriers() {
				if (!barriers.empty()) {
					list->ResourceBarrier((UINT)barriers.size(), barriers.data());
					stats.barriers += (uint32_t)barriers.size();
					barriers.clear();
				}
			}

			void GfxContext::FlushAsyncBarriers() {
				if (!async_barriers.empty()) {
					async_list->ResourceBarrier((UINT)async_barriers.size(), async_barriers.data());
					stats.barriers += (uint32_t)async_barriers.size();
					async_barriers.clear();
				}
			}

			void GfxContext::SetAsyncCompute(bool enabled) {
				async_enabled = enabled;
			}

			uint64_t GfxContext::Submit() {
				if (!frame_open) {
					return 0;
				}
				//What is open is closed: the compute list first, so the last direct list can end the frame joined to it.
				CloseAsync();
				if (!open && !seg_waits[GfxDevice::QUEUE_DIRECT].empty()) {
					OpenDirect();
				}
				CloseDirect();

				//The segments run in the order they were closed. Every wait refers to a segment that was closed before the one
				//that waits (MakeWait closes it first), so no queue is made to wait for a signal behind its own position.
				for (Segment& seg : segments) {
					gfx->ExecuteSegment(seg.queue, seg.list.Get(), seg.waits.data(), (UINT)seg.waits.size(), seg.value);
				}
				segments.clear();
				GfxDevice::QueueWait join = { GfxDevice::QUEUE_COMPUTE, compute_last_value };
				const uint64_t serial = gfx->FinishFrame(compute_last_value != 0 ? &join : nullptr);
				//The direct queue has waited for the compute queue's last segment, and everything that follows on it is behind that.
				if (compute_last_value > waited[GfxDevice::QUEUE_DIRECT][GfxDevice::QUEUE_COMPUTE]) {
					waited[GfxDevice::QUEUE_DIRECT][GfxDevice::QUEUE_COMPUTE] = compute_last_value;
				}
				slot_serial[slot] = serial;
				slot = (slot + 1) % GFX_FRAMES_IN_FLIGHT;
				frame_open = false;
				return serial;
			}

			void GfxContext::Flush() {
				const uint64_t serial = Submit();
				if (serial != 0) {
					gfx->WaitFrame(serial);
				}
			}

			//-------------------------------------------------------------------------------------------
			// Resource states
			//-------------------------------------------------------------------------------------------

			namespace {
				//What a compute list can put a resource in, and take it from: its barriers may only name these states.
				constexpr D3D12_RESOURCE_STATES COMPUTE_STATES =
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

				bool ComputeState(D3D12_RESOURCE_STATES state) {
					return (state & ~COMPUTE_STATES) == 0;
				}

				bool WritesState(D3D12_RESOURCE_STATES state) {
					return (state & ~READ_STATES) != 0;
				}

				//A resource in `before` can be used as `after` with no barrier: it is a state that only reads and `before`
				//holds it too, or it is the very state.
				bool Satisfies(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
					return before == after ||
						((after & ~READ_STATES) == 0 && (before & after) == after && (before & ~READ_STATES) == 0);
				}
			}

			//-------------------------------------------------------------------------------------------
			// Async compute: what one queue waits for from the other
			//-------------------------------------------------------------------------------------------

			//The commands queue q records from now on run after segment `value` of queue o. If that segment is still open
			//(or is the one about to open) it is closed first, so that it exists to be waited for and is executed before
			//the segment that waits.
			void GfxContext::MakeWait(int q, int o, uint64_t value) {
				if (value == 0 || value <= waited[q][o]) {
					return;
				}
				if (value >= seg_value[o]) {
					CloseQueue(o);
					value = seg_value[o] - 1;
					if (value == 0 || value <= waited[q][o]) {
						return;
					}
				}
				//What queue q has recorded in its open segment cannot be made to start after the wait: that list is closed
				//and the commands from here on go to the next one. A compute segment that holds nothing yet takes the wait
				//itself (a list is opened before the resources it is going to use are looked at).
				if (QueueOpen(q) && (q == GfxDevice::QUEUE_DIRECT || async_work)) {
					CloseQueue(q);
				}
				seg_waits[q].push_back({ o, value });
				waited[q][o] = value;
				++async_stats.waits;
			}

			//Queue q runs after everything queue o has recorded so far.
			void GfxContext::WaitForAll(int q, int o) {
				CloseQueue(o);
				MakeWait(q, o, seg_value[o] - 1);
			}

			void GfxContext::Register(GfxResource* r, bool write, int q) {
				if (!Tracking()) {
					return;
				}
				const uint64_t value = seg_value[q];
				if (write) {
					r->last_write_queue = (uint8_t)q;
					r->last_write_value = value;
					r->last_read_value[0] = r->last_read_value[1] = 0;
				}
				else {
					r->last_read_value[q] = value;
				}
			}

			//A use of `r` on queue q. Whatever the other queue did to it that conflicts has to be finished first: its write,
			//for a read or a write, and its reads, for a write.
			void GfxContext::Touch(GfxResource* r, bool write, int q) {
				EnsureOpenQueue(q);
				if (!Tracking()) {
					return;
				}
				const int o = 1 - q;
				uint64_t need = (r->last_write_queue == o) ? r->last_write_value : 0;
				if (write && r->last_read_value[o] > need) {
					need = r->last_read_value[o];
				}
				if (need > waited[q][o]) {
					MakeWait(q, o, need);
					EnsureOpenQueue(q);
				}
				Register(r, write, q);
			}

			//-------------------------------------------------------------------------------------------
			// Resource states
			//-------------------------------------------------------------------------------------------

			void GfxContext::UseImpl(GfxResource* r, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
				const bool whole = subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				const int q = rec_queue;
				const bool compute = q == GfxDevice::QUEUE_COMPUTE;
				//A shader read on the compute queue is a non-pixel read; the engine asks for both.
				const D3D12_RESOURCE_STATES wanted = (compute && after == GFX_STATE_SHADER_READ) ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : after;
				const auto set_state = [&](D3D12_RESOURCE_STATES state) {
					if (whole) { r->SetAllState(state); } else { r->SetSubresourceState(subresource, state); }
				};

				if (Satisfies(before, wanted)) {
					Touch(r, WritesState(wanted), q);
					if (wanted == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && r->uav_dirty) {
						D3D12_RESOURCE_BARRIER b = {};
						b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
						b.UAV.pResource = r->Get();
						(compute ? async_barriers : barriers).push_back(b);
						r->uav_dirty = false;
					}
					return;
				}

				D3D12_RESOURCE_BARRIER b = {};
				b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource = r->Get();
				b.Transition.Subresource = subresource;
				b.Transition.StateBefore = before;

				if (compute && !ComputeState(before)) {
					//The resource is in a state a compute list cannot leave (a render target, a depth buffer, a pixel shader
					//resource). The transition is recorded on the direct queue, which the compute queue is then made to wait
					//for (once, when every resource of the dispatch has been looked at: PrepareCompute). The resource goes to
					//the state the direct queue would have used, which also holds what the compute queue reads.
					Touch(r, true, GfxDevice::QUEUE_DIRECT);
					b.Transition.StateAfter = after;
					barriers.push_back(b);
					set_state(after);
					r->uav_dirty = false;
					direct_sync_pending = true;
					EnsureOpenAsync();
					Register(r, WritesState(wanted), GfxDevice::QUEUE_COMPUTE);
					return;
				}

				//A transition is a write: nobody else may be using the resource while it changes.
				Touch(r, true, q);
				b.Transition.StateAfter = wanted;
				(compute ? async_barriers : barriers).push_back(b);
				set_state(wanted);
				r->uav_dirty = false;
			}

			void GfxContext::UseSubresource(GfxResource* r, UINT subresource, D3D12_RESOURCE_STATES after) {
				if (r->heap_type != D3D12_HEAP_TYPE_DEFAULT || r->resource == nullptr || r->no_transitions) {
					return;
				}
				UseImpl(r, subresource, r->State(subresource), after);
			}

			void GfxContext::UseResource(GfxResource* r, D3D12_RESOURCE_STATES after) {
				if (r->heap_type != D3D12_HEAP_TYPE_DEFAULT || r->resource == nullptr || r->no_transitions) {
					return;
				}
				if (!r->IsSplit()) {
					UseImpl(r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, r->AllState(), after);
					return;
				}
				//Subresources are in different states: bring each to the one asked for, then they all agree.
				for (UINT s = 0; s < r->SubresourceCount(); ++s) {
					UseSubresource(r, s, after);
				}
				r->MergeIfUniform();
			}

			void GfxContext::UseView(const GfxView* view, D3D12_RESOURCE_STATES state) {
				GfxResource* r = view->Resource();
				if (r == nullptr) {
					return;
				}
				if (view->covers_all) {
					UseResource(r, state);
					return;
				}
				view->ForEachSubresource([&](UINT s) { UseSubresource(r, s, state); });
			}

			void GfxContext::Transition(GfxResource* resource, D3D12_RESOURCE_STATES state) {
				EnsureOpen();
				UseResource(resource, state);
				FlushBarriers();
			}

			bool GfxContext::ViewsOverlap(const GfxView* a, const GfxView* b) {
				if (a->Resource() != b->Resource()) {
					return false;
				}
				if (a->covers_all || b->covers_all) {
					return true;
				}
				const bool mips = a->first_mip < b->first_mip + b->mip_count && b->first_mip < a->first_mip + a->mip_count;
				const bool slices = a->first_slice < b->first_slice + b->slice_count && b->first_slice < a->first_slice + a->slice_count;
				return mips && slices;
			}

			//D3D11 unbinds a shader resource that is also bound for output when the output is set, leaving the slot
			//null. The engine's "unprepare" code only clears some of its slots, so passes rely on this.
			void GfxContext::UnbindConflicting(GfxResource* resource, const GfxView* view, bool compute) {
				//D3D11 checks the graphics pipeline (render targets against the VS..PS resources) and the compute pipeline
				//(UAVs against the CS resources) separately: a compute shader may read what the output merger still has
				//bound, and a draw may read what a finished dispatch wrote.
				for (UINT s = 0; s < GfxShader::STAGE_COUNT; ++s) {
					if ((s == GfxShader::CS) != compute) {
						continue;
					}
					StageState& st = stages[s];
					for (UINT i = 0; i < GFX_SRV_SLOTS; ++i) {
						GfxSRV* srv = st.srv[i];
						if (srv != nullptr && srv->Resource() == resource && ViewsOverlap(srv, view)) {
							st.srv[i] = nullptr;
							st.srv_dirty = true;
						}
					}
				}
			}

			//-------------------------------------------------------------------------------------------
			// State setting
			//-------------------------------------------------------------------------------------------

			void GfxContext::SetShader(GfxShader::Stage stage, GfxShader* shader) {
				StageState& st = stages[stage];
				if (st.shader.Get() == shader) {
					return;
				}
				st.shader = shader;
				st.srv_dirty = st.cb_dirty = st.sampler_dirty = st.uav_dirty = true;
			}

			void GfxContext::SetSRVs(GfxShader::Stage stage, UINT start, UINT count, GfxSRV* const* views) {
				StageState& st = stages[stage];
				for (UINT i = 0; i < count; ++i) {
					const UINT slot_index = start + i;
					if (slot_index >= GFX_SRV_SLOTS) {
						break;
					}
					GfxSRV* view = views ? views[i] : nullptr;
					if (view != nullptr) {
						//D3D11 refuses (nulls) a shader resource that is currently a render target, depth target
						//or compute UAV.
						bool conflict = false;
											const char* with = "";
											UINT with_slot = 0;
											for (UINT t = 0; t < render_target_count && !conflict && stage != GfxShader::CS; ++t) {
																	conflict = render_targets[t] != nullptr && ViewsOverlap(view, render_targets[t]);
																	with = "render target"; with_slot = t;
																}
																if (!conflict && stage != GfxShader::CS && depth_target != nullptr && !depth_target->read_only) {
																	conflict = ViewsOverlap(view, depth_target);
																	with = "depth target"; with_slot = 0;
																}
																if (!conflict && stage == GfxShader::CS) {
												const StageState& cs = stages[GfxShader::CS];
												for (UINT u = 0; u < GFX_UAV_SLOTS && !conflict; ++u) {
													conflict = cs.uav[u] != nullptr && ViewsOverlap(view, cs.uav[u]);
													with = "compute UAV"; with_slot = u;
												}
											}
											if (conflict) {
												static uint32_t reported = 0;
												if (reported < 40) {
													++reported;
													LOG_INFO("GfxContext: SRV stage %u slot %u refused: its resource %p is bound as %s %u", (unsigned)stage,
														(unsigned)slot_index, (void*)view->Resource(), with, with_slot);
												}
												view = nullptr;
											}
					}
					if (st.srv[slot_index].Get() != view) {
						st.srv[slot_index] = view;
						st.srv_dirty = true;
					}
				}
			}

			void GfxContext::CSSetBindlessSRVs(UINT count, GfxSRV* const* views) {
				count = (std::min)(count, GFX_BINDLESS_SLOTS);
				for (UINT i = 0; i < count; ++i) {
					GfxSRV* view = views ? views[i] : nullptr;
					if (cs_bindless[i].Get() != view) {
						cs_bindless[i] = view;
						cs_bindless_dirty = true;
					}
				}
				//What the previous list held past the end of this one is gone.
				for (UINT i = count; i < cs_bindless_count; ++i) {
					cs_bindless[i] = nullptr;
				}
				if (count != cs_bindless_count) {
					cs_bindless_count = count;
					cs_bindless_dirty = true;
				}
			}

			void GfxContext::SetCBs(GfxShader::Stage stage, UINT start, UINT count, GfxBuffer* const* buffers) {
				StageState& st = stages[stage];
				for (UINT i = 0; i < count; ++i) {
					const UINT slot_index = start + i;
					if (slot_index >= GFX_CBV_SLOTS) {
						break;
					}
					GfxBuffer* buffer = buffers ? buffers[i] : nullptr;
					if (st.cb[slot_index].Get() != buffer) {
						st.cb[slot_index] = buffer;
						st.cb_dirty = true;
					}
				}
			}

			void GfxContext::SetSamplers(GfxShader::Stage stage, UINT start, UINT count, GfxSampler* const* samplers) {
				StageState& st = stages[stage];
				for (UINT i = 0; i < count; ++i) {
					const UINT slot_index = start + i;
					if (slot_index >= GFX_SAMPLER_SLOTS) {
						break;
					}
					GfxSampler* sampler = samplers ? samplers[i] : nullptr;
					if (st.sampler[slot_index].Get() != sampler) {
						st.sampler[slot_index] = sampler;
						st.sampler_dirty = true;
					}
				}
			}

			void GfxContext::CSSetUnorderedAccessViews(UINT start, UINT count, GfxUAV* const* views) {
				StageState& st = stages[GfxShader::CS];
				for (UINT i = 0; i < count; ++i) {
					const UINT slot_index = start + i;
					if (slot_index >= GFX_UAV_SLOTS) {
						break;
					}
					GfxUAV* view = views ? views[i] : nullptr;
					if (view != nullptr) {
						UnbindConflicting(view->Resource(), view, true);
								}
								if (st.uav[slot_index].Get() != view) {
						st.uav[slot_index] = view;
						st.uav_dirty = true;
					}
				}
			}

			void GfxContext::IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY topology) {
				if (topology_state != topology) {
					topology_state = topology;
					topology_dirty = true;
				}
			}

			void GfxContext::IASetInputLayout(GfxInputLayout* layout) {
				input_layout = layout;
			}

			void GfxContext::IAGetInputLayout(GfxInputLayout** layout) const {
				*layout = input_layout.Get();
				if (*layout) {
					(*layout)->AddRef();
				}
			}

			void GfxContext::IASetVertexBuffers(UINT start_slot, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) {
				for (UINT i = 0; i < count; ++i) {
					const UINT s = start_slot + i;
					if (s >= 16) {
						break;
					}
					vertex_buffers[s] = buffers ? buffers[i] : nullptr;
					vertex_strides[s] = strides ? strides[i] : 0;
					vertex_offsets[s] = offsets ? offsets[i] : 0;
				}
				ia_dirty = true;
			}

			void GfxContext::IASetIndexBuffer(GfxBuffer* buffer, DXGI_FORMAT format, UINT offset) {
				index_buffer = buffer;
				index_format = format;
				index_offset = offset;
				ia_dirty = true;
			}

			void GfxContext::OMSetRenderTargets(UINT count, GfxRTV* const* rtvs, GfxDSV* dsv) {
				count = (std::min)(count, GFX_RTV_SLOTS);
				for (UINT i = 0; i < GFX_RTV_SLOTS; ++i) {
					render_targets[i] = (i < count && rtvs != nullptr) ? rtvs[i] : nullptr;
				}
				render_target_count = (rtvs != nullptr) ? count : 0;
					//D3D11 allowed a null target (the shadow passes bind one and only a depth buffer). D3D12 wants the count to
					//end at the last real target; a hole before it gets a null descriptor and an unknown format.
					while (render_target_count > 0 && render_targets[render_target_count - 1] == nullptr) {
						--render_target_count;
					}
					depth_target = dsv;
				om_dirty = true;
				for (UINT i = 0; i < render_target_count; ++i) {
					if (render_targets[i] != nullptr) {
						UnbindConflicting(render_targets[i]->Resource(), render_targets[i], false);
					}
				}
				if (depth_target != nullptr && !depth_target->read_only) {
					UnbindConflicting(depth_target->Resource(), depth_target, false);
				}
			}

			void GfxContext::OMGetRenderTargets(UINT count, GfxRTV** rtvs, GfxDSV** dsv) const {
				for (UINT i = 0; i < count; ++i) {
					if (rtvs != nullptr) {
						rtvs[i] = (i < GFX_RTV_SLOTS) ? render_targets[i].Get() : nullptr;
						if (rtvs[i]) { rtvs[i]->AddRef(); }
					}
				}
				if (dsv != nullptr) {
					*dsv = depth_target.Get();
					if (*dsv) { (*dsv)->AddRef(); }
				}
			}

			void GfxContext::OMSetBlendState(GfxBlendState* state, const float factor[4], UINT mask) {
				blend_state = state;
				if (factor != nullptr) {
					memcpy(blend_factor, factor, sizeof(blend_factor));
				}
				else {
					blend_factor[0] = blend_factor[1] = blend_factor[2] = blend_factor[3] = 1.0f;
				}
				sample_mask = mask;
				blend_dirty = true;
			}

			void GfxContext::OMGetBlendState(GfxBlendState** state, float factor[4], UINT* mask) const {
				if (state != nullptr) {
					*state = blend_state.Get();
					if (*state) { (*state)->AddRef(); }
				}
				if (factor != nullptr) {
					memcpy(factor, blend_factor, sizeof(blend_factor));
				}
				if (mask != nullptr) {
					*mask = sample_mask;
				}
			}

			void GfxContext::OMSetDepthStencilState(GfxDepthStencilState* state, UINT ref) {
				depth_state = state;
				if (stencil_ref != ref) {
					stencil_ref = ref;
					stencil_dirty = true;
				}
			}

			void GfxContext::OMGetDepthStencilState(GfxDepthStencilState** state, UINT* ref) const {
				if (state != nullptr) {
					*state = depth_state.Get();
					if (*state) { (*state)->AddRef(); }
				}
				if (ref != nullptr) {
					*ref = stencil_ref;
				}
			}

			void GfxContext::RSSetState(GfxRasterizerState* state) {
				if (raster_state.Get() != state) {
					raster_state = state;
					scissor_dirty = true;
				}
			}

			void GfxContext::RSGetState(GfxRasterizerState** state) const {
				*state = raster_state.Get();
				if (*state) { (*state)->AddRef(); }
			}

			void GfxContext::RSSetViewports(UINT count, const D3D12_VIEWPORT* v) {
				viewport_count = (std::min)(count, (UINT)D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
				if (v != nullptr && viewport_count > 0) {
					memcpy(viewports, v, sizeof(D3D12_VIEWPORT) * viewport_count);
				}
				viewport_dirty = true;
				scissor_dirty = true;
			}

			void GfxContext::RSGetViewports(UINT* count, D3D12_VIEWPORT* v) const {
				if (v != nullptr) {
					const UINT n = (std::min)(*count, viewport_count);
					memcpy(v, viewports, sizeof(D3D12_VIEWPORT) * n);
				}
				*count = viewport_count;
			}

			void GfxContext::RSSetScissorRects(UINT count, const D3D12_RECT* rects) {
				scissor_count = (std::min)(count, (UINT)D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
				if (rects != nullptr && scissor_count > 0) {
					memcpy(scissors, rects, sizeof(D3D12_RECT) * scissor_count);
				}
				scissor_dirty = true;
			}

			void GfxContext::RSGetScissorRects(UINT* count, D3D12_RECT* rects) const {
				if (rects != nullptr) {
					const UINT n = (std::min)(*count, scissor_count);
					memcpy(rects, scissors, sizeof(D3D12_RECT) * n);
				}
				*count = scissor_count;
			}

			//-------------------------------------------------------------------------------------------
			// Descriptors
			//-------------------------------------------------------------------------------------------

			bool GfxContext::AllocateDescriptors(DescriptorRing& ring, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
				D3D12_GPU_DESCRIPTOR_HANDLE& gpu) {
				if (ring.head + count + TABLE_MARGIN > ring.region_size) {
					return false;
				}
				const UINT64 index = (UINT64)ring.region * ring.region_size + ring.head;
				cpu = { ring.cpu_base.ptr + (SIZE_T)(index * ring.increment) };
				gpu = { ring.gpu_base.ptr + index * ring.increment };
				ring.head += count;
				return true;
			}

			//A table of samplers is built once per distinct set of samplers and kept for good: sampler heaps are tiny
			//(2048) and the engine has a handful of samplers, so the tables a frame needs are few.
			D3D12_GPU_DESCRIPTOR_HANDLE GfxContext::BuildSamplerTable(StageState& st) {
				const GfxShader* shader = st.shader;
				std::array<uint64_t, GFX_SAMPLER_SLOTS + 1> key = {};
				key[GFX_SAMPLER_SLOTS] = shader->sampler_count;
				for (UINT i = 0; i < shader->sampler_count; ++i) {
					key[i] = (st.sampler[i] != nullptr) ? st.sampler[i]->id : 0;
				}
				auto it = sampler_tables.find(key);
				if (it != sampler_tables.end()) {
					return it->second;
				}
				if (sampler_persistent_head + GFX_SAMPLER_SLOTS > sampler_persistent_size) {
					//Full. Tables cannot be recycled while commands that point at them are recorded, and this runs in
					//the middle of resolving a draw, so the draw gets the first table and the problem is reported.
					static bool reported = false;
					if (!reported) {
						LOG_ERROR("GfxContext: more than %u distinct sampler tables", sampler_persistent_size / GFX_SAMPLER_SLOTS);
						reported = true;
					}
					return { ring_sampler.gpu_base.ptr };
				}
				ID3D12Device* device = gfx->Device();
				const UINT first = sampler_persistent_head;
				for (UINT i = 0; i < GFX_SAMPLER_SLOTS; ++i) {
					D3D12_CPU_DESCRIPTOR_HANDLE dst = { ring_sampler.cpu_base.ptr + (SIZE_T)(first + i) * ring_sampler.increment };
					D3D12_CPU_DESCRIPTOR_HANDLE src = (i < shader->sampler_count && st.sampler[i] != nullptr)
						? st.sampler[i]->cpu : gfx->DefaultSampler();
					device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				}
				sampler_persistent_head += GFX_SAMPLER_SLOTS;
				D3D12_GPU_DESCRIPTOR_HANDLE handle = { ring_sampler.gpu_base.ptr + (UINT64)first * ring_sampler.increment };
				sampler_tables[key] = handle;
				return handle;
			}

			void GfxContext::RepublishConstantBuffer(GfxBuffer* buffer) {
				RingAllocation a = AllocateUpload(AlignUp(buffer->size, 256), 256);
				if (a.cpu == nullptr) {
					return;
				}
				memcpy(a.cpu, buffer->shadow.data(), (size_t)buffer->size);
				buffer->ring_va = a.gpu;
				buffer->ring_resource = a.resource;
				buffer->ring_offset = a.offset;
				buffer->ring_frame = gfx->FrameSerial();
			}

			GfxContext::RingAllocation GfxContext::AllocateUpload(UINT64 bytes, UINT64 alignment) {
				EnsureOpen();
				RingAllocation a;
				UINT64 head = AlignUp(upload.head, alignment);
				if (head + bytes <= upload.region_size) {
					const UINT64 base = upload.region * upload.region_size;
					a.cpu = upload.cpu + base + head;
					a.offset = base + head;
					a.resource = upload.resource.Get();
					a.gpu = upload.resource->GetGPUVirtualAddress() + a.offset;
					upload.head = head + bytes;
					return a;
				}

				//This frame's region of the ring is full. Take the memory from a one-off chunk instead.
				std::vector<OverflowChunk>& chunks = overflow[slot];
				if (chunks.empty() || AlignUp(chunks.back().head, alignment) + bytes > chunks.back().size) {
					OverflowChunk chunk;
					chunk.size = (std::max)(bytes, 16ull * 1024 * 1024);
					D3D12_HEAP_PROPERTIES hp = {};
					hp.Type = D3D12_HEAP_TYPE_UPLOAD;
					hp.CreationNodeMask = 1;
					hp.VisibleNodeMask = 1;
					D3D12_RESOURCE_DESC rd = {};
					rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					rd.Width = chunk.size;
					rd.Height = 1;
					rd.DepthOrArraySize = 1;
					rd.MipLevels = 1;
					rd.SampleDesc.Count = 1;
					rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
					if (FAILED(gfx->Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
						D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&chunk.resource)))) {
						LOG_ERROR("GfxContext: could not allocate a %llu byte upload chunk", (unsigned long long)chunk.size);
						return a;
					}
					D3D12_RANGE none = { 0, 0 };
					chunk.resource->Map(0, &none, (void**)&chunk.cpu);
					static bool reported = false;
					if (!reported) {
						LOG_WARN("GfxContext: a frame used more than %llu MB of upload memory", (unsigned long long)(upload.region_size >> 20));
						reported = true;
					}
					chunks.push_back(std::move(chunk));
				}
				OverflowChunk& chunk = chunks.back();
				const UINT64 at = AlignUp(chunk.head, alignment);
				a.cpu = chunk.cpu + at;
				a.offset = at;
				a.resource = chunk.resource.Get();
				a.gpu = chunk.resource->GetGPUVirtualAddress() + at;
				chunk.head = at + bytes;
				return a;
			}

			//-------------------------------------------------------------------------------------------
			// Buffers
			//-------------------------------------------------------------------------------------------

			void GfxContext::UpdateBuffer(GfxBuffer* buffer, const void* data, UINT64 bytes, UINT64 offset) {
				if (buffer == nullptr || data == nullptr || bytes == 0) {
					return;
				}
				switch (buffer->kind) {
				case GfxBufferKind::Constant: {
					if (offset + bytes > buffer->shadow.size()) {
						return;
					}
					memcpy(buffer->shadow.data() + offset, data, (size_t)bytes);
					//Published (copied into the ring) when something draws with it: a buffer written several times
					//between draws costs one copy.
					buffer->ring_frame = ~0ull;
					break;
				}
				case GfxBufferKind::Dynamic: {
					RingAllocation a = AllocateUpload(AlignUp(buffer->size, 256), 256);
					if (a.cpu == nullptr) {
						return;
					}
					memcpy(a.cpu, data, (size_t)bytes);
					buffer->ring_va = a.gpu;
					buffer->ring_resource = a.resource;
					buffer->ring_offset = a.offset;
					buffer->ring_frame = gfx->FrameSerial();
					//Whatever is bound as vertex or index data has moved.
					ia_dirty = true;
					break;
				}
				case GfxBufferKind::Upload:
					memcpy((uint8_t*)buffer->mapped + offset, data, (size_t)bytes);
					break;
				case GfxBufferKind::Static: {
					EnsureOpen();
					if (bytes > upload.region_size / 2) {
						//Too big for the ring: a one-off upload buffer that lives until the GPU is done with the frame.
						ComPtr<ID3D12Resource> staging;
						D3D12_HEAP_PROPERTIES hp = {};
						hp.Type = D3D12_HEAP_TYPE_UPLOAD;
						hp.CreationNodeMask = 1;
						hp.VisibleNodeMask = 1;
						D3D12_RESOURCE_DESC rd = {};
						rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
						rd.Width = bytes;
						rd.Height = 1;
						rd.DepthOrArraySize = 1;
						rd.MipLevels = 1;
						rd.SampleDesc.Count = 1;
						rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
						if (FAILED(gfx->Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
							D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) {
							LOG_ERROR("GfxContext: could not allocate a %llu byte upload buffer", (unsigned long long)bytes);
							return;
						}
						void* mapped = nullptr;
						D3D12_RANGE none = { 0, 0 };
						staging->Map(0, &none, &mapped);
						memcpy(mapped, data, (size_t)bytes);
						staging->Unmap(0, nullptr);
						UseResource(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
						FlushBarriers();
						list->CopyBufferRegion(buffer->Get(), offset, staging.Get(), 0, bytes);
						//The staging buffer has to outlive the copy: hand it to the deferred queue through a view-less
						//owner that is destroyed once this frame's serial completes.
						struct Keep : GfxObject {
							Keep() { gpu_owned = true; }
							ComPtr<ID3D12Resource> r;
						};
						Keep* keep = new Keep();
						keep->r = staging;
						keep->Release();
						break;
					}
					RingAllocation a = AllocateUpload(bytes, 16);
					if (a.cpu == nullptr) {
						return;
					}
					memcpy(a.cpu, data, (size_t)bytes);
					UseResource(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
					FlushBarriers();
					list->CopyBufferRegion(buffer->Get(), offset, a.resource, a.offset, bytes);
					//Back to where a read-only buffer lives, so its next use as vertex data, SRV or indirect
					//argument finds the state it already has.
					if ((buffer->resting_state & ~READ_STATES) == 0 && buffer->resting_state != D3D12_RESOURCE_STATE_COMMON) {
						UseResource(buffer, buffer->resting_state);
					}
					break;
				}
				default:
					break;
				}
			}

			void GfxContext::CopyResource(GfxResource* destination, GfxResource* source) {
				EnsureOpen();
				UseResource(destination, D3D12_RESOURCE_STATE_COPY_DEST);
				UseResource(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
				FlushBarriers();
				list->CopyResource(destination->Get(), source->Get());
			}

			void GfxContext::CopyBufferRegion(GfxBuffer* destination, UINT64 destination_offset, GfxBuffer* source,
				UINT64 source_offset, UINT64 bytes) {
				EnsureOpen();
				UseResource(destination, D3D12_RESOURCE_STATE_COPY_DEST);
				UseResource(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
				FlushBarriers();
				list->CopyBufferRegion(destination->Get(), destination_offset, source->Get(), source_offset, bytes);
			}

			void GfxContext::UpdateTexture(GfxTexture* texture, UINT subresource, const void* data, UINT row_pitch) {
				if (texture == nullptr || data == nullptr) {
					return;
				}
				EnsureOpen();
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
				UINT rows = 0;
				UINT64 row_size = 0;
				UINT64 total = 0;
				gfx->Device()->GetCopyableFootprints(&texture->desc, subresource, 1, 0, &layout, &rows, &row_size, &total);
				RingAllocation a = AllocateUpload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
				if (a.cpu == nullptr) {
					return;
				}
				const uint8_t* src = (const uint8_t*)data;
				for (UINT r = 0; r < rows; ++r) {
					memcpy((uint8_t*)a.cpu + (size_t)r * layout.Footprint.RowPitch, src + (size_t)r * row_pitch, (size_t)row_size);
				}
				layout.Offset = a.offset;
				UseSubresource(texture, subresource, D3D12_RESOURCE_STATE_COPY_DEST);
				FlushBarriers();
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = texture->Get();
				dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = subresource;
				D3D12_TEXTURE_COPY_LOCATION from = {};
				from.pResource = a.resource;
				from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				from.PlacedFootprint = layout;
				list->CopyTextureRegion(&dst, 0, 0, 0, &from, nullptr);
			}

			void GfxContext::CopyTextureToBuffer(GfxTexture* texture, GfxBuffer* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
				EnsureOpen();
				UseSubresource(texture, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
				FlushBarriers();
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = texture->Get();
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = 0;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = readback->Get();
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = footprint;
				list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}

			ID3D12GraphicsCommandList* GfxContext::BeginExternal() {
				EnsureOpen();
				FlushBarriers();
				return list.Get();
			}

			void GfxContext::EndExternal() {
				ID3D12DescriptorHeap* heaps[] = { ring_srv.heap.Get(), ring_sampler.heap.Get() };
				list->SetDescriptorHeaps(2, heaps);
				MarkAllDirty();
			}

			//-------------------------------------------------------------------------------------------
			// Queries
			//-------------------------------------------------------------------------------------------
			
						void GfxContext::Marker(const char* name) {
							if (!gfx->PixCapturerLoaded() || name == nullptr) {
								return;
							}
							EnsureOpen();
							//PIX_EVENT_ANSI_VERSION (1): the data is a null terminated ANSI string, its size in bytes with the null.
							list->SetMarker(1, name, (UINT)strlen(name) + 1);
						}

						void GfxContext::BeginQuery(GfxQueryHeap* heap, UINT index) {
							EnsureOpen();
							list->BeginQuery(heap->heap.Get(), heap->query_type, index);
						}
			
						void GfxContext::EndQuery(GfxQueryHeap* heap, UINT index) {
							EnsureOpen();
							list->EndQuery(heap->heap.Get(), heap->query_type, index);
						}
			
						void GfxContext::ResolveQueryData(GfxQueryHeap* heap, UINT first, UINT count) {
							EnsureOpen();
							FlushBarriers();
							list->ResolveQueryData(heap->heap.Get(), heap->query_type, first, count, heap->readback->Get(),
								(UINT64)first * heap->bytes_per_query);
							heap->ready_fence = FrameFence();
						}
			
						//-------------------------------------------------------------------------------------------
						// Clears
						//-------------------------------------------------------------------------------------------

			void GfxContext::ClearRenderTargetView(GfxRTV* rtv, const float color[4]) {
				if (rtv == nullptr) {
					return;
				}
				EnsureOpen();
				UseView(rtv, D3D12_RESOURCE_STATE_RENDER_TARGET);
				FlushBarriers();
				list->ClearRenderTargetView(rtv->Cpu(), color, 0, nullptr);
			}

			void GfxContext::ClearDepthStencilView(GfxDSV* dsv, UINT clear_flags, float depth, UINT8 stencil) {
				if (dsv == nullptr) {
					return;
				}
				EnsureOpen();
				UseView(dsv, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				FlushBarriers();
				UINT flags = clear_flags & (D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL);
				if (!HasStencil(dsv->Format())) {
					flags &= ~(UINT)D3D12_CLEAR_FLAG_STENCIL;
				}
				if (flags != 0) {
					list->ClearDepthStencilView(dsv->Cpu(), (D3D12_CLEAR_FLAGS)flags, depth, stencil, 0, nullptr);
				}
			}

			void GfxContext::ClearUnorderedAccessViewUint(GfxUAV* uav, const UINT values[4]) {
				if (uav == nullptr) {
					return;
				}
				EnsureOpen();
				UseView(uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				FlushBarriers();
				//The clear wants the descriptor in a shader-visible heap as well as the CPU copy.
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
				D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
				if (!AllocateDescriptors(ring_srv, 1, cpu, gpu)) {
					FlushBarriers();
					Flush();
					EnsureOpen();
					UseView(uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					FlushBarriers();
					AllocateDescriptors(ring_srv, 1, cpu, gpu);
				}
				gfx->Device()->CopyDescriptorsSimple(1, cpu, uav->Cpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				list->ClearUnorderedAccessViewUint(gpu, uav->Cpu(), uav->Resource()->Get(), values, 0, nullptr);
				uav->Resource()->uav_dirty = true;
			}

			void GfxContext::ClearUnorderedAccessViewFloat(GfxUAV* uav, const float values[4]) {
				if (uav == nullptr) {
					return;
				}
				EnsureOpen();
				UseView(uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				FlushBarriers();
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
				D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
				if (!AllocateDescriptors(ring_srv, 1, cpu, gpu)) {
					FlushBarriers();
					Flush();
					EnsureOpen();
					UseView(uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					FlushBarriers();
					AllocateDescriptors(ring_srv, 1, cpu, gpu);
				}
				gfx->Device()->CopyDescriptorsSimple(1, cpu, uav->Cpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				list->ClearUnorderedAccessViewFloat(gpu, uav->Cpu(), uav->Resource()->Get(), values, 0, nullptr);
				uav->Resource()->uav_dirty = true;
			}

			//-------------------------------------------------------------------------------------------
			// Pipelines
			//-------------------------------------------------------------------------------------------

			ID3D12PipelineState* GfxContext::GraphicsPipeline() {
				GfxShader* shaders[5] = {
					stages[GfxShader::VS].shader, stages[GfxShader::HS].shader, stages[GfxShader::DS].shader,
					stages[GfxShader::GS].shader, stages[GfxShader::PS].shader };
				GfxBlendState* blend = blend_state ? blend_state.Get() : default_blend.Get();
				GfxRasterizerState* raster = raster_state ? raster_state.Get() : default_raster.Get();
				GfxDepthStencilState* depth = depth_state ? depth_state.Get() : default_depth.Get();

				const D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = (shaders[1] != nullptr || shaders[2] != nullptr)
					? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : TopologyType(topology_state);
				const bool has_depth = depth_target != nullptr;

				GraphicsKey key;
				key.v.fill(0);
				for (int i = 0; i < 5; ++i) {
					key.v[i] = shaders[i] ? shaders[i]->id : 0;
				}
				key.v[5] = blend->id;
				key.v[6] = raster->id;
				key.v[7] = depth->id;
				key.v[8] = input_layout ? input_layout->id : 0;
				key.v[9] = (uint64_t)topology_type;
				key.v[10] = sample_mask;
				key.v[11] = render_target_count;
				for (UINT i = 0; i < render_target_count; ++i) {
					key.v[12 + i] = render_targets[i] ? (uint64_t)render_targets[i]->Format() : 0;
				}
				key.v[20] = has_depth ? (uint64_t)depth_target->Format() : 0;

				auto it = graphics_pipelines.find(key);
				if (it != graphics_pipelines.end()) {
					return it->second.Get();
				}

				D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
				d.pRootSignature = graphics_root.Get();
				if (shaders[0]) { d.VS = shaders[0]->Bytecode(); }
				if (shaders[1]) { d.HS = shaders[1]->Bytecode(); }
				if (shaders[2]) { d.DS = shaders[2]->Bytecode(); }
				if (shaders[3]) { d.GS = shaders[3]->Bytecode(); }
				if (shaders[4]) { d.PS = shaders[4]->Bytecode(); }
				d.BlendState = blend->desc;
				d.SampleMask = sample_mask;
				d.RasterizerState = raster->desc;
				d.DepthStencilState = depth->desc;
				if (!has_depth) {
					//D3D11 tested against nothing when no depth target was bound; D3D12 rejects a depth test with no
					//format.
					d.DepthStencilState.DepthEnable = FALSE;
					d.DepthStencilState.StencilEnable = FALSE;
				}
				if (input_layout != nullptr && !input_layout->elements.empty()) {
					d.InputLayout = { input_layout->elements.data(), (UINT)input_layout->elements.size() };
				}
				d.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
				d.PrimitiveTopologyType = topology_type;
				d.NumRenderTargets = render_target_count;
				for (UINT i = 0; i < render_target_count; ++i) {
					d.RTVFormats[i] = render_targets[i] ? render_targets[i]->Format() : DXGI_FORMAT_UNKNOWN;
				}
				d.DSVFormat = has_depth ? depth_target->Format() : DXGI_FORMAT_UNKNOWN;
				d.SampleDesc.Count = 1;
				d.SampleDesc.Quality = 0;
				d.NodeMask = 0;
				d.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

				ComPtr<ID3D12PipelineState> pso;
				const auto pso_begin = std::chrono::steady_clock::now();
				const HRESULT hr = gfx->Device()->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso));
				LogPipelineTime("graphics", pso_begin);
				if (FAILED(hr)) {
					LOG_ERROR("GfxContext: CreateGraphicsPipelineState failed (0x%08X): VS %llu HS %llu DS %llu GS %llu PS %llu, %u targets",
						(unsigned)hr, (unsigned long long)key.v[0], (unsigned long long)key.v[1], (unsigned long long)key.v[2],
						(unsigned long long)key.v[3], (unsigned long long)key.v[4], render_target_count);
				}
				++stats.pipelines_created;
				graphics_pipelines[key] = pso;
				return pso.Get();
			}

			ID3D12PipelineState* GfxContext::ComputePipeline() {
				GfxShader* cs = stages[GfxShader::CS].shader;
				auto it = compute_pipelines.find(cs->id);
				if (it != compute_pipelines.end()) {
					return it->second.Get();
				}
				D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
				d.pRootSignature = compute_root.Get();
				d.CS = cs->Bytecode();
				ComPtr<ID3D12PipelineState> pso;
				const auto pso_begin = std::chrono::steady_clock::now();
				const HRESULT hr = gfx->Device()->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
				LogPipelineTime("compute", pso_begin);
				if (FAILED(hr)) {
					LOG_ERROR("GfxContext: CreateComputePipelineState failed (0x%08X)", (unsigned)hr);
				}
				++stats.pipelines_created;
				compute_pipelines[cs->id] = pso;
				return pso.Get();
			}

			//-------------------------------------------------------------------------------------------
			// Resolving state at a draw or dispatch
			//-------------------------------------------------------------------------------------------

			bool GfxContext::ResolveStage(GfxShader::Stage stage, bool compute, ID3D12GraphicsCommandList* target) {
				StageState& st = stages[stage];
				GfxShader* shader = st.shader;
				if (shader == nullptr) {
					return true;
				}
				ID3D12Device* device = gfx->Device();

				//---- shader resources (already in the state they are read in: see ResolveGraphicsUses)
				if (st.srv_dirty && shader->srv_count > 0) {
					D3D12_CPU_DESCRIPTOR_HANDLE cpu;
					D3D12_GPU_DESCRIPTOR_HANDLE gpu;
					if (!AllocateDescriptors(ring_srv, shader->srv_count, cpu, gpu)) {
						return false;
					}
					D3D12_CPU_DESCRIPTOR_HANDLE sources[GFX_SRV_SLOTS];
					static const UINT ones[GFX_SRV_SLOTS] = {
						1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
						1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
						1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
						1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
					for (UINT i = 0; i < shader->srv_count; ++i) {
						if (st.srv[i] != nullptr) {
							sources[i] = st.srv[i]->Cpu();
						}
						else {
							sources[i] = gfx->NullSRV(shader->srv_used[i] ? shader->srv_dimension[i] : D3D12_SRV_DIMENSION_TEXTURE2D);
						}
					}
					device->CopyDescriptors(1, &cpu, &shader->srv_count, shader->srv_count, sources, ones,
						D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					stats.descriptors_copied += shader->srv_count;
					st.srv_table = gpu;
					st.srv_dirty = false;
					if (compute) {
						target->SetComputeRootDescriptorTable(COMPUTE_SRV, gpu);
					}
					else {
						target->SetGraphicsRootDescriptorTable(GraphicsParam(stage, PARAM_SRV), gpu);
					}
				}

				//---- constant buffers
				if (shader->cbv_count > 0) {
					bool rebuild = st.cb_dirty;
					for (UINT i = 0; i < shader->cbv_count; ++i) {
						GfxBuffer* cb = st.cb[i];
						if (!shader->cbv_used[i] || cb == nullptr) {
							continue;
						}
						if (cb->kind == GfxBufferKind::Constant && cb->ring_frame != gfx->FrameSerial()) {
							RepublishConstantBuffer(cb);
						}
						if (st.cb_bound[i] != cb->GpuAddress()) {
							rebuild = true;
						}
					}
					if (rebuild) {
						D3D12_CPU_DESCRIPTOR_HANDLE cpu;
						D3D12_GPU_DESCRIPTOR_HANDLE gpu;
						if (!AllocateDescriptors(ring_srv, shader->cbv_count, cpu, gpu)) {
							return false;
						}
						for (UINT i = 0; i < shader->cbv_count; ++i) {
							D3D12_CPU_DESCRIPTOR_HANDLE dst = { cpu.ptr + (SIZE_T)i * ring_srv.increment };
							GfxBuffer* cb = st.cb[i];
							if (shader->cbv_used[i] && cb != nullptr && cb->GpuAddress() != 0) {
								D3D12_CONSTANT_BUFFER_VIEW_DESC cd = {};
								cd.BufferLocation = cb->GpuAddress();
								cd.SizeInBytes = (UINT)AlignUp(cb->size, 256);
								device->CreateConstantBufferView(&cd, dst);
								st.cb_bound[i] = cb->GpuAddress();
							}
							else {
								device->CopyDescriptorsSimple(1, dst, gfx->NullCBV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
								st.cb_bound[i] = 0;
							}
						}
						stats.descriptors_copied += shader->cbv_count;
						st.cb_table = gpu;
						st.cb_dirty = false;
						if (compute) {
							target->SetComputeRootDescriptorTable(COMPUTE_CBV, gpu);
						}
						else {
							target->SetGraphicsRootDescriptorTable(GraphicsParam(stage, PARAM_CBV), gpu);
						}
					}
				}

				//---- samplers
				if (st.sampler_dirty && shader->sampler_count > 0) {
					const D3D12_GPU_DESCRIPTOR_HANDLE gpu = BuildSamplerTable(st);
					st.sampler_table = gpu;
					st.sampler_dirty = false;
					if (compute) {
						target->SetComputeRootDescriptorTable(COMPUTE_SAMPLER, gpu);
					}
					else {
						target->SetGraphicsRootDescriptorTable(GraphicsParam(stage, PARAM_SAMPLER), gpu);
					}
				}

				//---- unordered access (compute)
				if (compute) {
					if (st.uav_dirty && shader->uav_count > 0) {
						D3D12_CPU_DESCRIPTOR_HANDLE cpu;
						D3D12_GPU_DESCRIPTOR_HANDLE gpu;
						if (!AllocateDescriptors(ring_srv, shader->uav_count, cpu, gpu)) {
							return false;
						}
						D3D12_CPU_DESCRIPTOR_HANDLE sources[GFX_UAV_SLOTS];
						static UINT ones[GFX_UAV_SLOTS];
						static bool ones_set = false;
						if (!ones_set) {
							for (UINT& o : ones) { o = 1; }
							ones_set = true;
						}
						for (UINT i = 0; i < shader->uav_count; ++i) {
							if (st.uav[i] != nullptr) {
								sources[i] = st.uav[i]->Cpu();
							}
							else {
								sources[i] = gfx->NullUAV(shader->uav_used[i] ? shader->uav_dimension[i] : D3D12_UAV_DIMENSION_TEXTURE2D);
							}
						}
						device->CopyDescriptors(1, &cpu, &shader->uav_count, shader->uav_count, sources, ones,
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						stats.descriptors_copied += shader->uav_count;
						st.uav_table = gpu;
						st.uav_dirty = false;
						target->SetComputeRootDescriptorTable(COMPUTE_UAV, gpu);
					}
					//---- the unbounded texture array (a shader that indexes one)
					if (shader->bindless && cs_bindless_dirty) {
						const UINT count = (std::max)(cs_bindless_count, 1u);
						D3D12_CPU_DESCRIPTOR_HANDLE cpu;
						D3D12_GPU_DESCRIPTOR_HANDLE gpu;
						if (!AllocateDescriptors(ring_srv, count, cpu, gpu)) {
							return false;
						}
						static D3D12_CPU_DESCRIPTOR_HANDLE sources[GFX_BINDLESS_SLOTS];
						static UINT ones[GFX_BINDLESS_SLOTS];
						static bool ones_set = false;
						if (!ones_set) {
							for (UINT& o : ones) { o = 1; }
							ones_set = true;
						}
						for (UINT i = 0; i < count; ++i) {
							sources[i] = cs_bindless[i] != nullptr ? cs_bindless[i]->Cpu() : gfx->NullSRV(D3D12_SRV_DIMENSION_TEXTURE2D);
						}
						device->CopyDescriptors(1, &cpu, &count, count, sources, ones, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						stats.descriptors_copied += count;
						cs_bindless_dirty = false;
						target->SetComputeRootDescriptorTable(COMPUTE_BINDLESS, gpu);
					}
				}
				return true;
			}

			void GfxContext::TraceStage(const char* what, GfxShader::Stage stage) {
				const StageState& st = stages[stage];
				const GfxShader* sh = st.shader;
				if (sh == nullptr) {
					return;
				}
				std::string line = std::string(what) + " shader " + std::to_string(sh->id) + " srv[";
				for (UINT i = 0; i < sh->srv_count; ++i) {
					if (!sh->srv_used[i]) { continue; }
					char buf[96];
					if (st.srv[i] != nullptr) {
						snprintf(buf, sizeof(buf), " t%u=%p f%u s%x", i, (void*)st.srv[i]->Resource(), (unsigned)st.srv[i]->Format(),
							(unsigned)st.srv[i]->Resource()->State(0));
					}
					else {
						snprintf(buf, sizeof(buf), " t%u=NULL", i);
					}
					line += buf;
				}
				line += " ] uav[";
				for (UINT i = 0; i < sh->uav_count; ++i) {
					if (!sh->uav_used[i]) { continue; }
					char buf[96];
					if (st.uav[i] != nullptr) {
						snprintf(buf, sizeof(buf), " u%u=%p f%u s%x", i, (void*)st.uav[i]->Resource(), (unsigned)st.uav[i]->Format(),
							(unsigned)st.uav[i]->Resource()->State(0));
					}
					else {
						snprintf(buf, sizeof(buf), " u%u=NULL", i);
					}
					line += buf;
				}
				line += " ] cb[";
				for (UINT i = 0; i < sh->cbv_count; ++i) {
					if (!sh->cbv_used[i]) { continue; }
					char buf[64];
					snprintf(buf, sizeof(buf), " b%u=%llx", i, st.cb[i] ? (unsigned long long)st.cb[i]->GpuAddress() : 0ull);
					line += buf;
				}
				line += " ] smp[";
				for (UINT i = 0; i < sh->sampler_count; ++i) {
					if (!sh->sampler_used[i]) { continue; }
					line += st.sampler[i] ? " s" + std::to_string(i) + "=ok" : " s" + std::to_string(i) + "=NULL";
				}
				line += " ]";
				LOG_INFO("trace: %s", line.c_str());
			}

			void GfxContext::ReportSkipped(const char* what) {
				++stats.skipped;
				static uint32_t reported = 0;
				if (reported < 24) {
					++reported;
					LOG_WARN("GfxContext: a draw or dispatch was skipped: %s", what);
				}
			}

			bool GfxContext::PrepareGraphics() {
				for (int attempt = 0; attempt < 3; ++attempt) {
					EnsureOpen();
					if (stages[GfxShader::VS].shader == nullptr && stages[GfxShader::PS].shader == nullptr) {
								ReportSkipped("no vertex or pixel shader set");
								return false;
							}
					//Every resource is moved to its state before any command of the draw is recorded: that may make the
					//direct queue wait for the compute queue, which closes the list and starts another.
					if (!ResolveGraphicsUses()) {
						continue;
					}
					EnsureOpen();
					if (!graphics_root_set) {
						list->SetGraphicsRootSignature(graphics_root.Get());
						graphics_root_set = true;
					}

					//Pipeline.
					ID3D12PipelineState* pso = GraphicsPipeline();
							if (pso == nullptr) {
								ReportSkipped("the graphics pipeline state could not be created");
								return false;
							}

					bool exhausted = false;
					for (UINT s = 0; s < 5 && !exhausted; ++s) {
						exhausted = !ResolveStage((GfxShader::Stage)s, false, list.Get());
					}
					if (exhausted) {
						//The descriptor region is full: run what is recorded, wait for it, and resolve again against a
						//fresh region (everything is marked dirty by the reopen).
						FlushBarriers();
						Flush();
						continue;
					}

					FlushBarriers();
					if (pso != current_pipeline) {
						list->SetPipelineState(pso);
						current_pipeline = pso;
					}

					//Output merger.
					if (om_dirty) {
						D3D12_CPU_DESCRIPTOR_HANDLE handles[GFX_RTV_SLOTS] = {};
						for (UINT i = 0; i < render_target_count; ++i) {
							handles[i] = render_targets[i] ? render_targets[i]->Cpu() : gfx->NullRTV();
						}
						D3D12_CPU_DESCRIPTOR_HANDLE dsv = depth_target ? depth_target->Cpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
						list->OMSetRenderTargets(render_target_count, render_target_count ? handles : nullptr, FALSE,
							depth_target ? &dsv : nullptr);
						om_dirty = false;
					}
					if (viewport_dirty && viewport_count > 0) {
						list->RSSetViewports(viewport_count, viewports);
						viewport_dirty = false;
					}
					if (scissor_dirty) {
						GfxRasterizerState* raster = raster_state ? raster_state.Get() : default_raster.Get();
						if (raster->scissor_enable && scissor_count > 0) {
							list->RSSetScissorRects(scissor_count, scissors);
						}
						else {
							//No scissor: the whole of whatever the viewports cover.
							D3D12_RECT full = { 0, 0, 16384, 16384 };
							D3D12_RECT rects[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
							const UINT n = (std::max)(viewport_count, 1u);
							for (UINT i = 0; i < n; ++i) {
								rects[i] = full;
							}
							list->RSSetScissorRects(n, rects);
						}
						scissor_dirty = false;
					}
					if (blend_dirty) {
						list->OMSetBlendFactor(blend_factor);
						blend_dirty = false;
					}
					if (stencil_dirty) {
						list->OMSetStencilRef(stencil_ref);
						stencil_dirty = false;
					}
					if (topology_dirty) {
						list->IASetPrimitiveTopology(topology_state);
						topology_dirty = false;
					}
					if (ia_dirty) {
						D3D12_VERTEX_BUFFER_VIEW views[16] = {};
						UINT count = 0;
						for (UINT i = 0; i < 16; ++i) {
							GfxBuffer* vb = vertex_buffers[i];
							if (vb != nullptr) {
								views[i].BufferLocation = vb->GpuAddress() + vertex_offsets[i];
								views[i].SizeInBytes = (UINT)(vb->size - vertex_offsets[i]);
								views[i].StrideInBytes = vertex_strides[i];
								count = i + 1;
							}
						}
						if (count > 0) {
							list->IASetVertexBuffers(0, count, views);
						}
						if (index_buffer != nullptr) {
							D3D12_INDEX_BUFFER_VIEW iv = {};
							iv.BufferLocation = index_buffer->GpuAddress() + index_offset;
							iv.SizeInBytes = (UINT)(index_buffer->size - index_offset);
							iv.Format = index_format;
							list->IASetIndexBuffer(&iv);
						}
						ia_dirty = false;
					}
					return true;
				}
				return false;
			}

			bool GfxContext::PrepareCompute(bool async, GfxBuffer* indirect_args) {
				for (int attempt = 0; attempt < 3; ++attempt) {
					if (async) {
						EnsureOpenAsync();
					}
					else {
						EnsureOpen();
					}
					if (stages[GfxShader::CS].shader == nullptr) {
						ReportSkipped("no compute shader set");
						return false;
					}

					//Resources first (see PrepareGraphics): the uses are resolved for the queue the dispatch goes to, and if
					//that closed a list the work starts again on the one that is open now.
					const uint64_t cuts = cut_counter;
					rec_queue = async ? GfxDevice::QUEUE_COMPUTE : GfxDevice::QUEUE_DIRECT;
					direct_sync_pending = false;
					ResolveComputeUses(indirect_args);
					if (direct_sync_pending) {
						//Transitions were recorded on the direct list for this dispatch: it runs after them.
						direct_sync_pending = false;
						WaitForAll(GfxDevice::QUEUE_COMPUTE, GfxDevice::QUEUE_DIRECT);
					}
					rec_queue = GfxDevice::QUEUE_DIRECT;
					if (cut_counter != cuts) {
						continue;
					}
					if (async) {
						EnsureOpenAsync();
					}
					else {
						EnsureOpen();
					}

					ID3D12GraphicsCommandList* target = async ? async_list.Get() : list.Get();
					bool& root_set = async ? async_root_set : compute_root_set;
					ID3D12PipelineState*& bound_pipeline = async ? async_pipeline : current_pipeline;
					//The tables the stage last set were set on some list; if it was another one they are not there.
					StageState& cs = stages[GfxShader::CS];
					const uint64_t epoch = async ? async_epoch : direct_epoch;
					if (cs.table_epoch != epoch) {
						cs.srv_dirty = cs.cb_dirty = cs.sampler_dirty = cs.uav_dirty = true;
						cs_bindless_dirty = true;
						cs.srv_table = cs.cb_table = cs.sampler_table = cs.uav_table = {};
						for (UINT i = 0; i < GFX_CBV_SLOTS; ++i) {
							cs.cb_bound[i] = 0;
						}
						cs.table_epoch = epoch;
					}
					if (!root_set) {
						target->SetComputeRootSignature(compute_root.Get());
						root_set = true;
					}
					ID3D12PipelineState* pso = ComputePipeline();
					if (pso == nullptr) {
						ReportSkipped("the compute pipeline state could not be created");
						return false;
					}
					if (!ResolveStage(GfxShader::CS, true, target)) {
						if (async) { FlushAsyncBarriers(); } else { FlushBarriers(); }
						Flush();
						continue;
					}
					if (async) { FlushAsyncBarriers(); } else { FlushBarriers(); }
					if (pso != bound_pipeline) {
						target->SetPipelineState(pso);
						bound_pipeline = pso;
					}
					return true;
				}
				return false;
			}

			//-------------------------------------------------------------------------------------------
			// Resources a draw or a dispatch uses
			//-------------------------------------------------------------------------------------------

			void GfxContext::UseStageSRVs(GfxShader::Stage stage) {
				StageState& st = stages[stage];
				const GfxShader* shader = st.shader;
				if (shader == nullptr) {
					return;
				}
				//A compute shader reads in the non-pixel state: a texture only compute shaders read then stays in states the
				//compute queue can move it between, with no help from the direct one.
				const D3D12_RESOURCE_STATES read = (stage == GfxShader::CS) ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : GFX_STATE_SHADER_READ;
				for (UINT i = 0; i < shader->srv_count; ++i) {
					if (shader->srv_used[i] && st.srv[i] != nullptr) {
						UseView(st.srv[i], read);
					}
				}
				//The textures of the unbounded array are read too.
				if (stage == GfxShader::CS && shader->bindless) {
					for (UINT i = 0; i < cs_bindless_count; ++i) {
						if (cs_bindless[i] != nullptr) {
							UseView(cs_bindless[i], read);
						}
					}
				}
			}

			//False if resolving closed the list the draw was going to be recorded in: the caller starts again.
			bool GfxContext::ResolveGraphicsUses() {
				const uint64_t cuts = cut_counter;
				for (UINT i = 0; i < render_target_count; ++i) {
					if (render_targets[i] != nullptr) {
						UseView(render_targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
					}
				}
				if (depth_target != nullptr) {
					UseView(depth_target, depth_target->read_only ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
				}
				for (UINT i = 0; i < 16; ++i) {
					if (vertex_buffers[i] != nullptr) {
						UseResource(vertex_buffers[i], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
					}
				}
				if (index_buffer != nullptr) {
					UseResource(index_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
				}
				for (UINT s = 0; s < 5; ++s) {
					UseStageSRVs((GfxShader::Stage)s);
				}
				return cut_counter == cuts;
			}

			void GfxContext::ResolveComputeUses(GfxBuffer* indirect_args) {
				UseStageSRVs(GfxShader::CS);
				const StageState& st = stages[GfxShader::CS];
				const GfxShader* shader = st.shader;
				for (UINT i = 0; i < shader->uav_count; ++i) {
					if (shader->uav_used[i] && st.uav[i] != nullptr) {
						UseView(st.uav[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					}
				}
				if (indirect_args != nullptr) {
					UseResource(indirect_args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
				}
			}

void GfxContext::Draw(UINT vertex_count, UINT start_vertex) {
				if (PrepareGraphics()) {
					list->DrawInstanced(vertex_count, 1, start_vertex, 0);
					++stats.draws;
				}
			}

			void GfxContext::DrawInstanced(UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance) {
				if (PrepareGraphics()) {
					list->DrawInstanced(vertex_count, instance_count, start_vertex, start_instance);
					++stats.draws;
				}
			}

			void GfxContext::DrawIndexed(UINT index_count, UINT start_index, INT base_vertex) {
				if (PrepareGraphics()) {
					if (tracing) { TraceStage("drawindexed VS", GfxShader::VS); TraceStage("drawindexed PS", GfxShader::PS); }
					list->DrawIndexedInstanced(index_count, 1, start_index, base_vertex, 0);
					++stats.draws;
				}
			}

			void GfxContext::DrawIndexedInstanced(UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
				UINT start_instance) {
				if (PrepareGraphics()) {
					list->DrawIndexedInstanced(index_count, instance_count, start_index, base_vertex, start_instance);
					++stats.draws;
				}
			}

			//Records the dispatch itself into the list PrepareCompute got ready (the compute queue's, if `async`).
			template<class F>
			void GfxContext::RecordDispatch(bool async, F&& record) {
				record(async ? async_list.Get() : list.Get());
				++stats.dispatches;
				if (async) {
					async_work = true;
					++async_stats.dispatches;
				}
				const StageState& st = stages[GfxShader::CS];
				for (UINT i = 0; i < st.shader->uav_count; ++i) {
					if (st.shader->uav_used[i] && st.uav[i] != nullptr && st.uav[i]->Resource() != nullptr) {
						st.uav[i]->Resource()->uav_dirty = true;
					}
				}
			}

			void GfxContext::Dispatch(UINT x, UINT y, UINT z, bool async) {
				async = async && AsyncCompute();
				if (PrepareCompute(async, nullptr)) {
					if (tracing) { TraceStage("dispatch", GfxShader::CS); }
					RecordDispatch(async, [&](ID3D12GraphicsCommandList* target) { target->Dispatch(x, y, z); });
				}
			}

			void GfxContext::DispatchIndirect(GfxBuffer* args, UINT offset, bool async) {
				if (args == nullptr) {
					return;
				}
				async = async && AsyncCompute();
				if (PrepareCompute(async, args)) {
					RecordDispatch(async, [&](ID3D12GraphicsCommandList* target) {
						target->ExecuteIndirect(dispatch_signature.Get(), 1, args->Get(), offset, nullptr, 0);
					});
				}
			}
		}
	}
}
