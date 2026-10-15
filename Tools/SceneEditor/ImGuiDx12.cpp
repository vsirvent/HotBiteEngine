#include "ImGuiDx12.h"

#include <imgui_impl_dx12.h>
#include <vector>

using namespace HotBite::Engine;
using Microsoft::WRL::ComPtr;

namespace {
	// Descriptors a frame can hand out to textures; the editor shows a few hundred thumbnails at most.
	constexpr UINT FRAME_DESCRIPTORS = 4096;

	Core::DXCore* dx = nullptr;
	ComPtr<ID3D12DescriptorHeap> heap;
	UINT increment = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {};

	// The frame (GfxDevice::FrameSerial) the cursor below belongs to.
	uint64_t cursor_frame = ~0ull;
	UINT cursor = 0;
	// What was handed out this frame, kept alive and moved to the shader-read state before drawing.
	std::vector<GfxRef<GfxSRV>> used;

	UINT Region() {
		return (UINT)(dx->device->FrameSerial() % GFX_FRAMES_IN_FLIGHT);
	}
}

namespace ImGuiDx12 {
	bool Init(Core::DXCore* core) {
		dx = core;
		D3D12_DESCRIPTOR_HEAP_DESC d = {};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		// One for the font, then a region of FRAME_DESCRIPTORS per frame in flight.
		d.NumDescriptors = 1 + FRAME_DESCRIPTORS * GFX_FRAMES_IN_FLIGHT;
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dx->device->Device()->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap)))) {
			return false;
		}
		increment = dx->device->CbvSrvUavIncrement();
		cpu_start = heap->GetCPUDescriptorHandleForHeapStart();
		gpu_start = heap->GetGPUDescriptorHandleForHeapStart();
		return ImGui_ImplDX12_Init(dx->device->Device(), (int)GFX_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			heap.Get(), cpu_start, gpu_start);
	}

	void Shutdown() {
		ImGui_ImplDX12_Shutdown();
		used.clear();
		heap.Reset();
		dx = nullptr;
	}

	void NewFrame() {
		ImGui_ImplDX12_NewFrame();
	}

	ImTextureID TextureId(GfxSRV* srv) {
		if (srv == nullptr || dx == nullptr) {
			return (ImTextureID)nullptr;
		}
		const uint64_t frame = dx->device->FrameSerial();
		if (frame != cursor_frame) {
			cursor_frame = frame;
			cursor = 0;
			used.clear();
		}
		if (cursor >= FRAME_DESCRIPTORS) {
			return (ImTextureID)nullptr;
		}
		const UINT slot = 1 + Region() * FRAME_DESCRIPTORS + cursor++;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = { cpu_start.ptr + (SIZE_T)slot * increment };
		dx->device->Device()->CopyDescriptorsSimple(1, cpu, srv->Cpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		used.push_back(srv);
		return (ImTextureID)(gpu_start.ptr + (UINT64)slot * increment);
	}

	void Render(ImDrawData* draw_data) {
		if (dx == nullptr || draw_data == nullptr) {
			return;
		}
		GfxContext* context = dx->context;
		for (GfxRef<GfxSRV>& view : used) {
			context->TransitionView(view, GFX_STATE_SHADER_READ);
		}
		context->TransitionView(dx->RenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
		ID3D12GraphicsCommandList* list = context->BeginExternal();
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx->RenderTarget()->Cpu();
		list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		ID3D12DescriptorHeap* heaps[] = { heap.Get() };
		list->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(draw_data, list);
		context->EndExternal();
	}
}
