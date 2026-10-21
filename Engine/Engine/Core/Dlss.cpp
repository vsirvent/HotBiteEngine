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

#include "Dlss.h"

#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <vector>

#if defined(HOTBITE_DLSS)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

Dlss& Dlss::Get() {
	//Never destroyed: NGX must be shut down by the renderer, not by the static destructors.
	static Dlss* instance = new Dlss();
	return *instance;
}

#if defined(HOTBITE_DLSS)

namespace {
	//Names this engine to NGX. An identifier, not a secret: any GUID does for a custom engine.
	const char* const NGX_PROJECT_ID = "b4a1c6e0-6f3d-4e3a-9a52-8c1f0d2e7a11";
	const char* const NGX_ENGINE_VERSION = "1.0";
	constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;

	std::string ResultText(NVSDK_NGX_Result r) {
		char text[64] = {};
		snprintf(text, sizeof(text), "NGX error 0x%08X", (unsigned)r);
		return text;
	}
}

namespace {
	//Asks NGX which render sizes it accepts for an output of this size in the mode used for dynamic resolution.
	struct RenderRange {
		unsigned int optimal_w, optimal_h, max_w, max_h, min_w, min_h;
	};

	RenderRange QueryRange(NVSDK_NGX_Parameter* caps, int out_w, int out_h) {
		RenderRange r = { (unsigned int)out_w, (unsigned int)out_h, (unsigned int)out_w, (unsigned int)out_h, (unsigned int)out_w, (unsigned int)out_h };
		float sharpness = 0.0f;
		const NVSDK_NGX_Result q = NGX_DLSS_GET_OPTIMAL_SETTINGS(caps, (unsigned int)out_w, (unsigned int)out_h,
			NVSDK_NGX_PerfQuality_Value_MaxQuality, &r.optimal_w, &r.optimal_h, &r.max_w, &r.max_h, &r.min_w, &r.min_h, &sharpness);
		if (NVSDK_NGX_FAILED(q)) {
			//An old dll that does not say: the mode's own size, and half of it.
			r.max_w = (unsigned int)out_w;
			r.max_h = (unsigned int)out_h;
			r.min_w = r.max_w / 2;
			r.min_h = r.max_h / 2;
		}
		return r;
	}
}

struct Dlss::Impl {
	NVSDK_NGX_Parameter* params = nullptr;
	NVSDK_NGX_Handle* feature = nullptr;
	int feature_width = 0;
	int feature_height = 0;
	bool ngx_started = false;
	ID3D12Device* device = nullptr;
	//The render size the feature is made for (the biggest of the range).
	unsigned int range_max_w = 0;
	unsigned int range_max_h = 0;
};

bool Dlss::Init() {
	if (initialized) {
		return available;
	}
	initialized = true;
	available = false;
	DXCore* dx = DXCore::Get();
	if (dx == nullptr || dx->device == nullptr || dx->device->Device() == nullptr) {
		reason = "the graphics device is not ready";
		initialized = false;
		return false;
	}
	GfxDevice* gfx = dx->device;
	DXGI_ADAPTER_DESC1 adapter = {};
	if (gfx->Adapter() == nullptr || FAILED(gfx->Adapter()->GetDesc1(&adapter)) || adapter.VendorId != NVIDIA_VENDOR_ID) {
		reason = "the graphics card is not an NVIDIA one";
		return false;
	}
	impl = new Impl();
	impl->device = gfx->Device();

	//Where NGX looks for nvngx_dlss.dll besides the folder of the program: HOTBITE_DLSS_DIR, then the folder of the SDK this
	//engine was built with (a development machine; a shipped game carries the dll next to its exe).
	std::vector<std::wstring> folders;
	{
		wchar_t value[512] = {};
		size_t len = 0;
		if (_wgetenv_s(&len, value, 512, L"HOTBITE_DLSS_DIR") == 0 && len > 1) {
			folders.emplace_back(value);
		}
		//This file is Engine/Engine/Core/Dlss.cpp in the checkout the engine was built from; the SDK is a submodule of it.
		std::error_code ec;
		const std::filesystem::path sdk = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "ThirdParty" / "DLSS" / "lib" / "Windows_x86_64" / "rel";
		if (std::filesystem::exists(sdk, ec)) {
			folders.emplace_back(std::filesystem::weakly_canonical(sdk, ec).wstring());
		}
	}
	std::vector<const wchar_t*> paths;
	for (const std::wstring& f : folders) {
		paths.push_back(f.c_str());
	}
	NVSDK_NGX_FeatureCommonInfo info = {};
	info.PathListInfo.Path = paths.empty() ? nullptr : paths.data();
	info.PathListInfo.Length = (unsigned int)paths.size();
	info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;

	std::error_code ec;
	std::filesystem::path data = std::filesystem::temp_directory_path(ec) / "HotBiteEngine" / "ngx";
	std::filesystem::create_directories(data, ec);

	NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init_with_ProjectID(NGX_PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, NGX_ENGINE_VERSION,
		data.wstring().c_str(), impl->device, &info, NVSDK_NGX_Version_API);
	if (NVSDK_NGX_FAILED(r)) {
		reason = "NGX could not start (" + ResultText(r) + "); is nvngx_dlss.dll next to the program?";
		delete impl;
		impl = nullptr;
		return false;
	}
	impl->ngx_started = true;

	NVSDK_NGX_Parameter* caps = nullptr;
	r = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
	if (NVSDK_NGX_FAILED(r) || caps == nullptr) {
		reason = "NGX did not report its capabilities (" + ResultText(r) + "); the driver is too old";
		Shutdown();
		return false;
	}
	int needs_driver = 0;
	int super_sampling = 0;
	caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
	caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &super_sampling);
	if (needs_driver == 0 && super_sampling != 0) {
		const int out_w = dx->GetWidth();
		const int out_h = dx->GetHeight();
		const RenderRange range = QueryRange(caps, out_w, out_h);
		min_scale = (std::min)(1.0f, (float)range.min_w / (float)out_w);
		max_scale = (std::min)(1.0f, (float)range.max_w / (float)out_w);
		impl->range_max_w = range.max_w;
		impl->range_max_h = range.max_h;
		char text[160] = {};
		snprintf(text, sizeof(text), "NGX accepts render sizes %ux%u .. %ux%u (optimal %ux%u) for %dx%d", range.min_w, range.min_h,
			range.max_w, range.max_h, range.optimal_w, range.optimal_h, out_w, out_h);
		this->info = text;
		LOG_INFO("Dlss: %s", text);
	}
	NVSDK_NGX_D3D12_DestroyParameters(caps);
	if (needs_driver != 0) {
		reason = "DLSS needs a newer NVIDIA driver";
		Shutdown();
		return false;
	}
	if (super_sampling == 0) {
		reason = "DLSS is not available on this graphics card or without nvngx_dlss.dll";
		Shutdown();
		return false;
	}
	r = NVSDK_NGX_D3D12_AllocateParameters(&impl->params);
	if (NVSDK_NGX_FAILED(r) || impl->params == nullptr) {
		reason = "NGX could not allocate its parameters (" + ResultText(r) + ")";
		Shutdown();
		return false;
	}
	available = true;
	reason.clear();
	return true;
}

void Dlss::Shutdown() {
	if (impl == nullptr) {
		available = false;
		return;
	}
	if (impl->feature != nullptr) {
		NVSDK_NGX_D3D12_ReleaseFeature(impl->feature);
		impl->feature = nullptr;
	}
	if (impl->params != nullptr) {
		NVSDK_NGX_D3D12_DestroyParameters(impl->params);
		impl->params = nullptr;
	}
	if (impl->ngx_started) {
		NVSDK_NGX_D3D12_Shutdown1(impl->device);
	}
	delete impl;
	impl = nullptr;
	available = false;
	if (reason.empty()) {
		reason = "DLSS was shut down";
	}
}

bool Dlss::Evaluate(const Frame& frame) {
	if (!available || impl == nullptr || frame.color == nullptr || frame.depth == nullptr || frame.motion == nullptr || frame.output == nullptr) {
		return false;
	}
	GfxContext* context = DXCore::Get()->context;

	//Every input is read by a compute pass of NGX and the output written by one.
	context->Transition(frame.color, GFX_STATE_SHADER_READ);
	context->Transition(frame.depth, GFX_STATE_SHADER_READ);
	context->Transition(frame.motion, GFX_STATE_SHADER_READ);
	context->Transition(frame.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	ID3D12GraphicsCommandList* list = context->BeginExternal();
	bool ok = true;

	if (impl->feature != nullptr && (impl->feature_width != frame.output_width || impl->feature_height != frame.output_height)) {
		//The output changed size: the old feature is of no use. It is released once the GPU is done with what it recorded.
		NVSDK_NGX_D3D12_ReleaseFeature(impl->feature);
		impl->feature = nullptr;
	}
	if (impl->feature == nullptr) {
		NVSDK_NGX_DLSS_Create_Params create = {};
		//Dynamic resolution: a mode fixes the range of render sizes the feature accepts (see Init); the feature is made
		//for the biggest of them and every frame is then drawn anywhere inside the range. (DLAA, the native size alone,
		//accepts no other size.)
		unsigned int max_w = impl->range_max_w != 0 ? impl->range_max_w : (unsigned int)frame.output_width;
		unsigned int max_h = impl->range_max_h != 0 ? impl->range_max_h : (unsigned int)frame.output_height;
		create.Feature.InWidth = max_w;
		create.Feature.InHeight = max_h;
		create.Feature.InTargetWidth = (unsigned int)frame.output_width;
		create.Feature.InTargetHeight = (unsigned int)frame.output_height;
		create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
		//The picture is display-referred (it is clamped to 0..1 by the mixer), so no HDR; its exposure is not known to the
		//engine, so DLSS measures it; the motion vectors are in render pixels.
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		create.InEnableOutputSubrects = false;
		const NVSDK_NGX_Result r = NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &impl->feature, impl->params, &create);
		if (NVSDK_NGX_FAILED(r)) {
			reason = "DLSS could not create its feature (" + ResultText(r) + ")";
			LOG_ERROR("Dlss: %s", reason.c_str());
			impl->feature = nullptr;
			ok = false;
		}
		else {
			impl->feature_width = frame.output_width;
			impl->feature_height = frame.output_height;
		}
	}

	if (ok) {
		NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
		eval.Feature.pInColor = frame.color->Get();
		eval.Feature.pInOutput = frame.output->Get();
		eval.pInDepth = frame.depth->Get();
		eval.pInMotionVectors = frame.motion->Get();
		eval.InJitterOffsetX = frame.jitter_x;
		eval.InJitterOffsetY = frame.jitter_y;
		eval.InRenderSubrectDimensions.Width = (unsigned int)frame.render_width;
		eval.InRenderSubrectDimensions.Height = (unsigned int)frame.render_height;
		eval.InReset = frame.reset ? 1 : 0;
		//The motion vectors are already in pixels.
		eval.InMVScaleX = 1.0f;
		eval.InMVScaleY = 1.0f;
		const NVSDK_NGX_Result r = NGX_D3D12_EVALUATE_DLSS_EXT(list, impl->feature, impl->params, &eval);
		if (NVSDK_NGX_FAILED(r)) {
			reason = "DLSS failed to upscale (" + ResultText(r) + ")";
			LOG_ERROR("Dlss: %s", reason.c_str());
			ok = false;
		}
	}
	//NGX set its own heaps, root signature and pipeline on the list.
	context->EndExternal();
	return ok;
}

#else

//Built without the SDK: the engine knows nothing of DLSS, so there is no dynamic resolution.
struct Dlss::Impl {};

bool Dlss::Init() {
	initialized = true;
	available = false;
	reason = "this build of the engine has no DLSS (the ThirdParty/DLSS submodule was not checked out)";
	return false;
}

void Dlss::Shutdown() {
	available = false;
}

bool Dlss::Evaluate(const Frame&) {
	return false;
}

#endif
