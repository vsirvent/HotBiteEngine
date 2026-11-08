#include "GBufferEffect.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

void GBufferEffect::Process(GfxContext* context, const GBufferViews& views) {
	if (enabled) {
		Render(context, views);
	}
	if (next != nullptr) {
		next->Process(context, views);
	}
}

GenericGBufferEffect::GenericGBufferEffect(const std::string& cs) {
	shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>(cs);
}

void GenericGBufferEffect::SetShaderResourceView(const std::string& name, GfxSRV* srv) {
	if (srv == nullptr) {
		srvs.erase(name);
	}
	else {
		srvs[name] = srv;
	}
}

namespace {
	//The G-buffer by the names a stage's shader declares, in GBufferViews' order.
	const char* const GBUFFER_UAV_NAMES[] = { "scene_out", "light_out", "bloom_out", "rt_ray0_out", "rt_ray1_out",
		"prev_position_out", "position_out", "material_out", "depth_out" };
}

void GenericGBufferEffect::Render(GfxContext* context, const GBufferViews& views) {
	if (shader == nullptr || views.camera == nullptr || views.width <= 0 || views.height <= 0) {
		return;
	}
	const Components::Camera& camera = *views.camera;
	shader->SetInt("screenW", views.width);
	shader->SetInt("screenH", views.height);
	shader->SetFloat("time", views.time);
	shader->SetFloat3("cameraPosition", camera.world_position);
	shader->SetMatrix4x4("view_inverse", camera.inverse_view);
	shader->SetMatrix4x4("projection_inverse", camera.inverse_projection);
	shader->SetMatrix4x4("view_projection", camera.view_projection);
	SetConstants(shader, views);
	if (views.sampler != nullptr) {
		shader->SetSamplerState("basicSampler", views.sampler);
	}
	for (const auto& srv : srvs) {
		shader->SetShaderResourceView(srv.first, srv.second);
	}
	shader->SetShaderResourceView("sceneSource", views.scene_source);
	GfxUAV* const uavs[] = { views.scene, views.light, views.bloom, views.ray0, views.ray1, views.prev_position, views.position,
		views.material, views.depth };
	for (size_t i = 0; i < sizeof(uavs) / sizeof(uavs[0]); ++i) {
		shader->SetUnorderedAccessView(GBUFFER_UAV_NAMES[i], uavs[i]);
	}
	shader->CopyAllBufferData();
	shader->SetShader();
	context->Dispatch((views.width + 7) / 8, (views.height + 7) / 8, 1);
	for (const auto& srv : srvs) {
		shader->SetShaderResourceView(srv.first, nullptr);
	}
	shader->SetShaderResourceView("sceneSource", nullptr);
	for (const char* name : GBUFFER_UAV_NAMES) {
		shader->SetUnorderedAccessView(name, nullptr);
	}
	context->CSSetShader(nullptr);
}
