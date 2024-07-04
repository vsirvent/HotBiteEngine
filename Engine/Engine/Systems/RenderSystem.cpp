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

#include <Components/Physics.h>
#include <Components/Sky.h>
#include <Core/Vertex.h>
#include <Core/SimpleShader.h>
#include <Core/Utils.h>
#include "RenderSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

const std::string RenderSystem::WORLD = "world";
const std::string RenderSystem::PREV_WORLD = "prevWorld";
const std::string RenderSystem::MESH_NORMAL_MAP = "meshNormalTexture";
const std::string RenderSystem::MESH_NORMAL_MAP_ENABLE = "meshNormalTextureEnable";
const std::string RenderSystem::AMBIENT_LIGHT = "ambientLight";
const std::string RenderSystem::DIRLIGHT_COUNT = "dirLightsCount";
const std::string RenderSystem::DIR_LIGHTS = "dirLights";
const std::string RenderSystem::POINT_LIGHT_COUNT = "pointLightsCount";
const std::string RenderSystem::POINT_LIGHTS = "pointLights";
const std::string RenderSystem::LIGHT_PERSPECTIVE_VALUES = "LightPerspectiveValues";
const std::string RenderSystem::POINT_SHADOW_MAP_TEXTURE = "PointShadowMapTexture[0]";
const std::string RenderSystem::DIR_PERSPECTIVE_VALUES = "DirPerspectiveMatrix";
const std::string RenderSystem::DIR_STATIC_PERSPECTIVE_VALUES = "DirStaticPerspectiveMatrix";
const std::string RenderSystem::DIR_SHADOW_MAP_TEXTURE = "DirShadowMapTexture[0]";
const std::string RenderSystem::DIR_STATIC_SHADOW_MAP_TEXTURE = "DirStaticShadowMapTexture[0]";
const std::string RenderSystem::HIGH_TEXTURE = "highTexture";
const std::string RenderSystem::AO_TEXTURE = "aoTexture";
const std::string RenderSystem::ARM_TEXTURE = "armTexture";
const std::string RenderSystem::EMISSION_TEXTURE = "emissionTexture";
const std::string RenderSystem::OPACITY_TEXTURE = "opacityTexture";
const std::string RenderSystem::HIGH_TEXTURE_ENABLED = "highTextureEnable";
const std::string RenderSystem::NORMAL_TEXTURE = "normalTexture";
const std::string RenderSystem::SPEC_TEXTURE = "specularTexture";
const std::string RenderSystem::MATERIAL = "material";
const std::string RenderSystem::DIFFUSE_TEXTURE = "diffuseTexture";
const std::string RenderSystem::DEPTH_TEXTURE = "depthTexture";
const std::string RenderSystem::CAMERA_POSITION = "cameraPosition";
const std::string RenderSystem::CAMERA_DIRECTION = "cameraDirection";
const std::string RenderSystem::TESS_ENABLED = "tessEnabled";
const std::string RenderSystem::VIEW = "view";
const std::string RenderSystem::PROJECTION = "projection";
const std::string RenderSystem::ACTIVE_VIEWS = "activeViews";
const std::string RenderSystem::CUBE_VIEW_0 = "CubeViewProj0";
const std::string RenderSystem::CUBE_VIEW_1 = "CubeViewProj1";
const std::string RenderSystem::CUBE_VIEW_2 = "CubeViewProj2";
const std::string RenderSystem::CUBE_VIEW_3 = "CubeViewProj3";
const std::string RenderSystem::CUBE_VIEW_4 = "CubeViewProj4";
const std::string RenderSystem::CUBE_VIEW_5 = "CubeViewProj5";
const std::string RenderSystem::BASIC_SAMPLER = "basicSampler";
const std::string RenderSystem::PCF_SAMPLER = "PCFSampler";
const std::string RenderSystem::SCREEN_W = "screenW";
const std::string RenderSystem::SCREEN_H = "screenH";
const std::string RenderSystem::TIME = "time";
const std::string RenderSystem::LIGHT_TEXTURE = "lightTexture";
const std::string RenderSystem::TESS_FACTOR = "tessFactor";
const std::string RenderSystem::TESS_TYPE = "tessType";
const std::string RenderSystem::DISPLACEMENT_SCALE = "displacementScale";
std::recursive_mutex RenderSystem::mutex;

void RenderSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	sky_signature.set(coordinator->GetComponentType<Material>(), true);
	sky_signature.set(coordinator->GetComponentType<Sky>(), true);
	sky_signature.set(coordinator->GetComponentType<Transform>(), true);
	sky_signature.set(coordinator->GetComponentType<Mesh>(), true);
	sky_signature.set(coordinator->GetComponentType<Lighted>(), true);

	amblight_signature.set(coordinator->GetComponentType<Base>(), true);
	amblight_signature.set(coordinator->GetComponentType<AmbientLight>(), true);


	dirlight_signature.set(coordinator->GetComponentType<Base>(), true);
	dirlight_signature.set(coordinator->GetComponentType<DirectionalLight>(), true);

	plight_signature.set(coordinator->GetComponentType<Base>(), true);
	plight_signature.set(coordinator->GetComponentType<Transform>(), true);
	plight_signature.set(coordinator->GetComponentType<PointLight>(), true);

	camera_signature.set(coordinator->GetComponentType<Base>(), true);
	camera_signature.set(coordinator->GetComponentType<Transform>(), true);
	camera_signature.set(coordinator->GetComponentType<Camera>(), true);

	drawable_signature.set(coordinator->GetComponentType<Base>(), true);
	drawable_signature.set(coordinator->GetComponentType<Transform>(), true);
	drawable_signature.set(coordinator->GetComponentType<Mesh>(), true);
	drawable_signature.set(coordinator->GetComponentType<Material>(), true);
	drawable_signature.set(coordinator->GetComponentType<Lighted>(), true);
	drawable_signature.set(coordinator->GetComponentType<Bounds>(), true);

	particles_signature.set(coordinator->GetComponentType<Base>(), true);
	particles_signature.set(coordinator->GetComponentType<Transform>(), true);
	particles_signature.set(coordinator->GetComponentType<Particles>(), true);
}

void RenderSystem::OnEntityDestroyed(ECS::Entity entity) {
	ambient_lights.Remove(entity);
	point_lights.Remove(entity);
	directional_lights.Remove(entity);
	cameras.Remove(entity);

	RemoveDrawable(entity, render_pass2_tree);
	RemoveDrawable(entity, render_tree);
	RemoveDrawable(entity, depth_tree);
	RemoveDrawable(entity, shadow_tree);
	RemoveParticle(entity, particle_tree);
}

void RenderSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {
	if ((entity_signature & sky_signature) == sky_signature)
	{
		skies.Insert(entity, SkyEntity{ coordinator, entity });
	}
	else
	{
		skies.Remove(entity);
	}
	if ((entity_signature & amblight_signature) == amblight_signature)
	{
		ambient_lights.Insert(entity, AmbientLightEntity{ coordinator, entity });
	}
	else
	{
		ambient_lights.Remove(entity);
	}
	if ((entity_signature & dirlight_signature) == dirlight_signature)
	{
		directional_lights.Insert(entity, DirectionalLightEntity{ coordinator, entity });
	}
	else
	{
		directional_lights.Remove(entity);
	}
	if ((entity_signature & plight_signature) == plight_signature)
	{
		point_lights.Insert(entity,PointLightEntity{ coordinator, entity });
	}
	else
	{
		point_lights.Remove(entity);
	}
	if ((entity_signature & camera_signature) == camera_signature)
	{
		cameras.Insert(entity, CameraEntity{ coordinator, entity });
	}
	else
	{
		cameras.Remove(entity);
	}

	if ((entity_signature & particles_signature) == particles_signature) {
		ParticleEntity part{ coordinator, entity };
		for (auto& p : part.particles->data.GetData()) {
			MaterialData* mat = p->GetMaterial();
			ShaderKey key{ mat->shaders.vs, mat->shaders.hs, mat->shaders.ds, mat->shaders.gs, mat->shaders.ps };
			AddParticle(entity, key, mat, particle_tree, part);
		}
	}
	else {
		RemoveParticle(entity, particle_tree);
	}

	bool is_pass1 = false;
	bool is_pass2 = false;

	//Entities including Sky component are rendered in a specific call, avoid adding to this map
	if ((entity_signature & drawable_signature) == drawable_signature &&
		(entity_signature & sky_signature) != sky_signature)
	{
		DrawableEntity drawable{ coordinator, entity };
		MaterialData* mat = drawable.mat->data;
		ShaderKey key{ mat->shaders.vs, mat->shaders.hs, mat->shaders.ds, mat->shaders.gs, mat->shaders.ps };
		if (drawable.base->pass == 1) {
			AddDrawable(entity, key, drawable.mat, render_tree, drawable);
			ShaderKey depth_key{ mat->depth_shaders.vs, mat->depth_shaders.hs, mat->depth_shaders.ds, mat->depth_shaders.gs, mat->depth_shaders.ps };
			AddDrawable(entity, depth_key, drawable.mat, depth_tree, drawable);
			is_pass1 = true;
		}
		else if (drawable.base->pass == 2) {
			AddDrawable(entity, key, drawable.mat, render_pass2_tree, drawable);
			is_pass2 = true;
		}
		else {
			assert("Invalid render pass");
		}
		if (is_pass1 || is_pass2) {
			ShaderKey shadow_key{ mat->shadow_shaders.vs, mat->shadow_shaders.hs, mat->shadow_shaders.ds, mat->shadow_shaders.gs, mat->shadow_shaders.ps };
			AddDrawable(entity, shadow_key, drawable.mat, shadow_tree, drawable);
		}
		if (!is_pass2) {
			RemoveDrawable(entity, render_pass2_tree);
		}
		if (!is_pass1) {
			RemoveDrawable(entity, render_tree);
			RemoveDrawable(entity, depth_tree);
		}
		if (!is_pass1 && !is_pass2) {
			RemoveDrawable(entity, shadow_tree);
		}
	}
	else {
		RemoveDrawable(entity, render_pass2_tree);
		RemoveDrawable(entity, render_tree);
		RemoveDrawable(entity, depth_tree);
		RemoveDrawable(entity, shadow_tree);
	}
}

bool RenderSystem::Init(DXCore* dx_core, Core::VertexBuffer<Vertex>* vb, Core::BVHBuffer* bvh)
{
	bool ret = true;
	try {
		dxcore = dx_core;
		vertex_buffer = vb;
		bvh_buffer = bvh;
		tbvh_buffer.Prepare(MAX_OBJECTS * 2 - 1);
		first_pass_target = dxcore;		
		depth_target = dxcore;
		int w = dxcore->GetWidth();
		int h = dxcore->GetHeight();
		
		if (FAILED(light_map.Init(w, h))) {
			throw std::exception("light_map.Init failed");
		}
		if (FAILED(vol_light_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("light_map.Init failed");
		}
		if (FAILED(lens_flare_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("lens_flare_map.Init failed");
		}
		if (FAILED(dust_render_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("dust_render_map.Init failed");
		}
		if (FAILED(vol_light_map2.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("light_map.Init failed");
		}
		if (FAILED(position_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT))) {
			throw std::exception("position_map.Init failed");
		}
		if (FAILED(prev_position_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT))) {
			throw std::exception("prev_position_map.Init failed");
		}
		if (FAILED(bloom_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("bloom_map.Init failed");
		}
		if (FAILED(temp_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("temp_map.Init failed");
		}
		if (FAILED(depth_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("depth_map.Init failed");
		}
		if (FAILED(depth_view.Init(w / 2, h / 2))) {
			throw std::exception("depth_view.Init failed");
		}
		if (FAILED(first_pass_texture.Init(w, h))) {
			throw std::exception("first_pass_texture.Init failed");
		}
		if (FAILED(motion_texture.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("motion_texture.Init failed");
		}
		if (FAILED(rgba_noise_texture.Init(RGA_NOISE_W, RGBA_NOISE_H, DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, (const uint8_t*)rgba_noise_map, RGBA_NOISE_LEN))) {
			throw std::exception("rgba_noise_texture.Init failed");
		}
		for (int x = 0; x < RT_NTEXTURES; ++x) {
			for (int i = 0; i < RT_NTEXTURES; ++i) {
				if (FAILED(rt_texture[x][i].Init(w / TEXTURE_RESOLUTION_DIVIDER, h / TEXTURE_RESOLUTION_DIVIDER, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
					throw std::exception("rt_texture.Init failed");
				}
			}
		}
		for (int i = 0; i < RT_NTEXTURES; ++i) {
			if (FAILED(rt_texture_out[i].Init(w / TEXTURE_RESOLUTION_DIVIDER, h / TEXTURE_RESOLUTION_DIVIDER, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
				throw std::exception("rt_texture.Init failed");
			}
		}

		if (FAILED(rt_texture_props.Init(w/ TEXTURE_RESOLUTION_DIVIDER, h/ TEXTURE_RESOLUTION_DIVIDER, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("rt_texture_props.Init failed");
		}
		if (FAILED(texture_tmp.Init(w/ TEXTURE_RESOLUTION_DIVIDER, h/ TEXTURE_RESOLUTION_DIVIDER, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("texture_tmp.Init failed");
		}
		if (FAILED(motion_blur_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("texture_tmp.Init failed");
		}

		if (FAILED(rt_ray_sources0.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("rt_ray_sources0.Init failed");
		}
		if (FAILED(rt_ray_sources1.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("rt_ray_sources1.Init failed");
		}
		if (FAILED(vol_data.Init(16, 16, DXGI_FORMAT::DXGI_FORMAT_R32_UINT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("vol_data.Init failed");
		}
		lens_flare = ShaderFactory::Get()->GetShader<SimpleComputeShader>("LensFlareCS.cso");
		if (lens_flare == nullptr) {
			throw std::exception("lens_flare shader.Init failed");
		}
		dust_init = ShaderFactory::Get()->GetShader<SimpleComputeShader>("InitDustCS.cso");
		if (dust_init == nullptr) {
			throw std::exception("dust_init shader.Init failed");
		}
		dust_update = ShaderFactory::Get()->GetShader<SimpleComputeShader>("UpdateDustCS.cso");
		if (dust_update == nullptr) {
			throw std::exception("dust_update shader.Init failed");
		}		
		dust_render = ShaderFactory::Get()->GetShader<SimpleComputeShader>("RenderDustCS.cso");
		if (dust_render == nullptr) {
			throw std::exception("dust_render shader.Init failed");
		}
		rt_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("RayTraceCS.cso");
		if (rt_shader == nullptr) {
			throw std::exception("raytrace shader.Init failed");
		}
		vol_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("VolumetricLightCS.cso");
		if (vol_shader == nullptr) {
			throw std::exception("VolumetricLightCS shader.Init failed");
		}
		blur_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("BlurCS.cso");
		if (blur_shader == nullptr) {
			throw std::exception("BlurCS shader.Init failed");
		}
		rt_smooth = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SmoothCS.cso");
		if (rt_smooth == nullptr) {
			throw std::exception("SmoothCS shader.Init failed");
		}
		motion_blur = ShaderFactory::Get()->GetShader<SimpleComputeShader>("MotionBlurCS.cso");
		if (motion_blur == nullptr) {
			throw std::exception("motion blur shader.Init failed");
		}
		mixer_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("TextureMixerCS.cso");
		if (mixer_shader == nullptr) {
			throw std::exception("mixer shader.Init failed");
		}
		aa_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("AntiAliasCS.cso");
		if (mixer_shader == nullptr) {
			throw std::exception("AA shader.Init failed");
		}
		motion_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("MotionCS.cso");
		if (motion_shader == nullptr) {
			throw std::exception("Motion shader.Init failed");
		}
	}
	catch (std::exception& e) {
		printf("RenderSystem::Init: ERROR: %s\n", e.what());
		ret = false;
	}
	return ret;
}

RenderSystem::~RenderSystem() {
	depth_map.Release();
	depth_view.Release();
	light_map.Release();
	vol_light_map.Release();
	vol_light_map2.Release();
	dust_render_map.Release();
	lens_flare_map.Release();
	position_map.Release();
	prev_position_map.Release();
	bloom_map.Release();
	dust_map.Release();
	temp_map.Release();
	first_pass_texture.Release();
	rgba_noise_texture.Release();
	motion_texture.Release();
	for (int x = 0; x < 2; ++x) {
		for (int i = 0; i < RT_NTEXTURES; ++i) {
			rt_texture[x][i].Release();
		}
	}
	for (int i = 0; i < RT_NTEXTURES; ++i) {
		rt_texture_out[i].Release();
	}
	texture_tmp.Release();
	rt_texture_props.Release();
	rt_ray_sources0.Release();
	rt_ray_sources1.Release();
	vol_data.Release();
}

void RenderSystem::AddDrawable(ECS::Entity entity, const Core::ShaderKey& key, Components::Material* mat, RenderTree& tree, const RenderSystem::DrawableEntity& drawable) {
	tree[key][mat->data].second.Insert(entity, drawable);
	tree[key][mat->data].first = mat;
	for (auto shader_it = tree.begin(); shader_it != tree.end(); ++shader_it) {
		for (auto mat_it = shader_it->second.begin(); mat_it != shader_it->second.end(); ++mat_it) {
			if (mat_it->first != mat->data) {
				mat_it->second.second.Remove(entity);
			}
		}
	}
}

void RenderSystem::AddParticle(ECS::Entity entity, const Core::ShaderKey& key, Core::MaterialData* mat, RenderParticleTree& tree, const RenderSystem::ParticleEntity& particle) {
	tree[key][mat].second.Insert(entity, particle);
	tree[key][mat].first = mat;
}

void RenderSystem::RemoveParticle(ECS::Entity entity, RenderParticleTree& tree) {
	std::map < Core::ShaderKey, std::list<std::map<Core::MaterialData*,
		std::pair<Core::MaterialData*, ECS::EntityVector<ParticleEntity > > >::iterator>> to_remove;
	for (auto shader_it = tree.begin(); shader_it != tree.end(); ++shader_it) {
		for (auto mat_it = shader_it->second.begin(); mat_it != shader_it->second.end(); ++mat_it) {
			mat_it->second.second.Remove(entity);
			if (mat_it->second.second.GetData().empty()) {
				to_remove[shader_it->first].push_back(mat_it);
			}
		}
	}
	for (auto const& shader : to_remove) {
		for (auto const& mat_it : shader.second) {
			tree[shader.first].erase(mat_it);
		}
	}
}

void RenderSystem::RemoveDrawable(ECS::Entity entity, RenderTree& tree) {
	std::unordered_map < Core::ShaderKey, std::list<std::unordered_map<Core::MaterialData*,
		std::pair<Components::Material*, ECS::EntityVector<DrawableEntity > > >::iterator>> to_remove;
	for (auto shader_it = tree.begin(); shader_it != tree.end(); ++shader_it) {
		for (auto mat_it = shader_it->second.begin(); mat_it != shader_it->second.end(); ++mat_it) {
			mat_it->second.second.Remove(entity);
			if (mat_it->second.second.GetData().empty()) {
				to_remove[shader_it->first].push_back(mat_it);
			}
		}
	}
	for (auto const& shader : to_remove) {
		for (auto const& mat_it : shader.second) {
			tree[shader.first].erase(mat_it);
		}
	}
}

void RenderSystem::DrawDepth(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection) {

	ID3D11DeviceContext* context = dxcore->context;
	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];
	
	SimpleVertexShader* vs = nullptr;
	SimpleHullShader* hs = nullptr;
	SimpleDomainShader* ds = nullptr;
	SimpleGeometryShader* gs = nullptr;
	SimplePixelShader* ps = nullptr;

	context->GSSetShader(nullptr, nullptr, 0);
	ID3D11RenderTargetView* rv[1] = { depth_map.RenderTarget() };
	context->OMSetRenderTargets(1, rv, depth_view.Depth());
	context->RSSetViewports(1, &dxcore->half_viewport);
	context->RSSetState(dxcore->drawing_rasterizer);

	SkyEntity* sky = nullptr;
	if (!skies.GetData().empty()) {
		sky = &(skies.GetData()[0]);
	}

	float time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;
	for (auto &shaders: depth_tree) {

		SimpleVertexShader* new_vs = std::get<SHADER_KEY_VS>(shaders.first);
		if (vs != new_vs) {
			vs = new_vs;
			if (vs) {
				vs->SetMatrix4x4(VIEW, cam_entity.camera->view);
				vs->SetFloat(TIME, time);
				vs->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				vs->SetShader();
			}
			else {
				context->VSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleHullShader* new_hs = std::get<SHADER_KEY_HS>(shaders.first);
		if (hs != new_hs) {
			hs = new_hs;
			if (hs) {
				hs->SetShader();
				hs->SetFloat(TIME, time);
			}
			else {
				context->HSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleDomainShader* new_ds = std::get<SHADER_KEY_DS>(shaders.first);
		if (ds != new_ds) {
			ds = new_ds;
			if (ds) {
				ds->SetShader();
				ds->SetFloat(TIME, time);
			}
			else {
				context->DSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleGeometryShader* new_gs = std::get<SHADER_KEY_GS>(shaders.first);
		if (gs != new_gs) {
			if (gs) {
				gs->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
				gs->SetFloat(TIME, time);
			}
			gs = new_gs;
			if (gs) {				
				gs->SetShader();
			}
			else {
				context->GSSetShader(nullptr, nullptr, 0);
			}
		}
		SimplePixelShader* new_ps = std::get<SHADER_KEY_PS>(shaders.first);
		if (ps != new_ps) {
			ps = new_ps;
			if (ps) {
				ps->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				ps->SetFloat(TIME, time);
				ps->SetShader();
			}
			else {
				context->PSSetShader(nullptr, nullptr, 0);
			}
		}
		ShaderKey sk{ vs, hs, ds, gs, ps };
		Event e(this, EVENT_ID_DEPTH_PREPARE_SHADER);
		e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, sk);
		coordinator->SendEvent(e);
		for (auto &mat: shaders.second) {
			if (!(mat.first->props.flags & (ALPHA_ENABLED_FLAG | BLEND_ENABLED_FLAG))) {
				for (auto &de : mat.second.second.GetData()) {
					if (de.base->visible && de.base->draw_depth) {
						PrepareEntity(de, vs, hs, ds, gs, ps);
						Mesh* mesh = de.mesh;						
						DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);						
						UnprepareEntity(de, vs, hs, ds, gs, ps);
					}
				}
			}
		}
		context->GenerateMips(depth_map.SRV());
		e.SetType(EVENT_ID_DEPTH_UNPREPARE_SHADER);
		coordinator->SendEvent(e);
	}
}

void RenderSystem::CastShadows(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, bool static_shadows) {

	ID3D11DeviceContext* context = dxcore->context;
	context->PSSetShader(nullptr, 0, 0);
	CameraEntity& cam_entity = cameras.GetData()[0];

	SimpleVertexShader* vs = nullptr;
	SimpleHullShader* hs = nullptr;
	SimpleDomainShader* ds = nullptr;
	SimplePixelShader* ps = nullptr;
	SimpleGeometryShader* gs = nullptr;
	float time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;

	if (!static_shadows) {
		for (PointLightEntity& l : point_lights.GetData()) {
			if (l.light->CastShadow()) {
				//Render to depth texture					
				ID3D11RenderTargetView* rtv[1] = { nullptr };
				context->OMSetRenderTargets(1, rtv, l.light->DepthView());
				context->ClearDepthStencilView(
					l.light->DepthView(),
					D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
					1.0f, 0);
				context->RSSetViewports(1, &l.light->GetShadowViewPort());
				context->RSSetState(dxcore->shadow_rasterizer);
				float range_p2 = l.light->GetData().range * l.light->GetData().range;


				for (auto& shaders : shadow_tree) {
					SimpleVertexShader* new_vs = std::get<SHADER_KEY_VS>(shaders.first);
					if (vs != new_vs) {
						vs = new_vs;
						if (vs) {
							vs->SetFloat(TIME, time);
							vs->SetShader();
						}
						else {
							context->VSSetShader(nullptr, nullptr, 0);
						}
					}
					SimpleHullShader* new_hs = std::get<SHADER_KEY_HS>(shaders.first);
					if (hs != new_hs) {
						hs = new_hs;
						if (hs) {
							hs->SetFloat(TIME, time);
							hs->SetShader();
						}
						else {
							context->HSSetShader(nullptr, nullptr, 0);
						}
					}
					SimpleDomainShader* new_ds = std::get<SHADER_KEY_DS>(shaders.first);
					if (ds != new_ds) {
						ds = new_ds;
						if (ds) {
							ds->SetFloat(TIME, time);
							ds->SetShader();
						}
						else {
							context->DSSetShader(nullptr, nullptr, 0);
						}
					}
					SimpleGeometryShader* new_gs = std::get<SHADER_KEY_GS>(shaders.first);
					if (gs != new_gs) {
						gs = new_gs;
						if (gs) {
							gs->SetShader();
						}
						else {
							context->GSSetShader(nullptr, nullptr, 0);
						}
					}

					if (gs) {
						const float4x4* viewProj = l.light->GetViewMatrix();
						gs->SetInt(ACTIVE_VIEWS, 0x3F);
						gs->SetFloat(TIME, time);
						gs->SetMatrix4x4(CUBE_VIEW_0, viewProj[0]);
						gs->SetMatrix4x4(CUBE_VIEW_1, viewProj[1]);
						gs->SetMatrix4x4(CUBE_VIEW_2, viewProj[2]);
						gs->SetMatrix4x4(CUBE_VIEW_3, viewProj[3]);
						gs->SetMatrix4x4(CUBE_VIEW_4, viewProj[4]);
						gs->SetMatrix4x4(CUBE_VIEW_5, viewProj[5]);
						gs->CopyAllBufferData();
					}

					SimplePixelShader* new_ps = std::get<SHADER_KEY_PS>(shaders.first);
					if (ps != new_ps) {
						ps = new_ps;
						if (ps) {
							ps->SetShader();
						}
						else {
							context->PSSetShader(nullptr, nullptr, 0);
						}
					}
					ShaderKey sk{ vs, hs, ds, gs, ps };
					Event e(this, EVENT_ID_SHADOW_POINT_LIGHT_PREPARE_SHADER);
					e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, sk);
					coordinator->SendEvent(e);
					for (auto& mat : shaders.second) {
						for (DrawableEntity& de : mat.second.second.GetData()) {
							if (de.base->visible && de.base->cast_shadow && DIST2((l.light->GetData().position - de.transform->position)) < range_p2) {
								PrepareEntity(de, vs, hs, ds, gs, ps);
								Mesh* mesh = de.mesh;
								DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);
								UnprepareEntity(de, vs, hs, ds, gs, ps);
							}
						}
					}
					e.SetType(EVENT_ID_SHADOW_POINT_LIGHT_UNPREPARE_SHADER);
					coordinator->SendEvent(e);
				}
			}
		}
	}
	
	for (DirectionalLightEntity& l : directional_lights.GetData()) {
		if (l.light->CastShadow()) {
			//Render to depth texture
			ID3D11RenderTargetView* rtv[1] = { nullptr };
			ID3D11DepthStencilView* dv = nullptr;
			if (static_shadows) {
				l.light->RefreshStaticViewMatrix();
				dv = l.light->StaticDepthView();
			}
			else {
				dv = l.light->DepthView();
			}
			context->ClearDepthStencilView(
				dv,
				D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
				1.0f, 0);
			context->OMSetRenderTargets(1, rtv, dv);
			context->RSSetViewports(1, &l.light->GetShadowViewPort());
			context->RSSetState(dxcore->dir_shadow_rasterizer);
			
			for (auto &shaders: shadow_tree) {
				SimpleVertexShader* new_vs = std::get<SHADER_KEY_VS>(shaders.first);
				if (vs != new_vs) {
					vs = new_vs;
					if (vs) {
						vs->SetFloat(TIME, time);
						vs->SetShader();
					}
					else {
						context->VSSetShader(nullptr, nullptr, 0);
					}
				}
				SimpleHullShader* new_hs = std::get<SHADER_KEY_HS>(shaders.first);
				if (hs != new_hs) {
					hs = new_hs;
					if (hs) {
						hs->SetFloat(TIME, time);
						hs->SetShader();
					}
					else {
						context->HSSetShader(nullptr, nullptr, 0);
					}
				}
				SimpleDomainShader* new_ds = std::get<SHADER_KEY_DS>(shaders.first);
				if (ds != new_ds) {
					ds = new_ds;
					if (ds) {
						ds->SetFloat(TIME, time);
						ds->SetShader();
					}
					else {
						context->DSSetShader(nullptr, nullptr, 0);
					}
				}
				SimpleGeometryShader* new_gs = std::get<SHADER_KEY_GS>(shaders.first);
				if (gs != new_gs) {
					gs = new_gs;
					if (gs) {
						gs->SetShader();
					}
					else {
						context->GSSetShader(nullptr, nullptr, 0);
					}
				}

				if (gs) {
					gs->SetFloat(TIME, time);
					gs->SetInt(ACTIVE_VIEWS, 0x01);
					gs->SetMatrix4x4(CUBE_VIEW_0, *l.light->GetViewMatrix());
					gs->CopyAllBufferData();
				}

				SimplePixelShader* new_ps = std::get<SHADER_KEY_PS>(shaders.first);
				if (ps != new_ps) {
					ps = new_ps;
					if (ps) {
						ps->SetFloat(TIME, time);
						ps->SetShader();
					}
					else {
						context->PSSetShader(nullptr, nullptr, 0);
					}
				}
				ShaderKey sk{ vs, hs, ds, gs, ps };
				Event e(this, EVENT_ID_SHADOW_DIR_LIGHT_PREPARE_SHADER);
				e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, sk);
				coordinator->SendEvent(e);

				for (auto &mat: shaders.second) {
					for (auto& de: mat.second.second.GetData()) {
						if (de.base->visible && de.base->cast_shadow && !l.light->IsSkipEntity(de.base->id) &&
							//we paint static objects if static_shadows = true || dynamic objects if static_shadows = false
							((!static_shadows && !de.base->is_static) || (static_shadows && de.base->is_static))) {
							PrepareEntity(de, vs, hs, ds, gs, ps);
							Mesh* mesh = de.mesh;
							DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);						
							UnprepareEntity(de, vs, hs, ds, gs, ps);
						}
					}
				}
				e.SetType(EVENT_ID_SHADOW_DIR_LIGHT_UNPREPARE_SHADER);
				coordinator->SendEvent(e);
			}
		}		
	}
	context->GSSetShader(nullptr, 0, 0);
}

void RenderSystem::DrawSky(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection) {
	
	if (skies.GetData().empty()) {
		return;
	}

	assert(skies.GetData().size() == 1 && "More than one sky registered.");
	SkyEntity& sky = skies.GetData()[0];
	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];
	ID3D11DeviceContext* context = dxcore->context;

	float time = ((float)Scheduler::Get()->GetElapsedNanoSeconds() * sky.sky->second_speed) / 1000000000.0f;
	//Render sky background
	{
		ID3D11RenderTargetView* rv[2] = { first_pass_target->RenderTarget(), light_map.RenderTarget() };
		context->OMSetRenderTargets(2, rv, nullptr);
		context->OMSetBlendState(dxcore->blend, NULL, ~0U);
		context->RSSetViewports(1, &dxcore->viewport);
		context->RSSetState(dxcore->sky_rasterizer);

		SimpleVertexShader* vs = sky.mat->data->shaders.vs;
		SimplePixelShader* ps = sky.mat->data->shaders.ps;
		assert(vs != nullptr && "Sky with no vertex shader.");
		assert(ps != nullptr && "Sky with no pixel shader.");
		vs->SetShader();
		ps->SetShader();
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);

		vs->SetMatrix4x4(VIEW, cam_entity.camera->view);
		vs->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		vs->SetMatrix4x4(WORLD, sky.transform->world_matrix);

		ps->SetInt(SCREEN_W, w);
		ps->SetInt(SCREEN_H, h);
		ps->SetMatrix4x4(VIEW, cam_entity.camera->view);
		ps->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		ps->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		ps->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		ps->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
		ps->SetFloat(TIME, time);
		ps->SetFloat3("back_color", sky.sky->current_backcolor);
		float space_intensity = 1.0f - directional_lights.GetData()[0].light->GetData().intensity;
		ps->SetFloat("space_intensity", space_intensity);
		if (sky.sky->space_mesh != nullptr && sky.sky->space_material != nullptr && space_intensity > 0.0f) {
			PrepareMaterial(sky.sky->space_material, vs, nullptr, nullptr, nullptr, ps);
			ps->SetInt(SimpleShaderKeys::IS_SPACE, 1);
			vs->SetInt(SimpleShaderKeys::IS_SPACE, 1);
			
			vs->CopyAllBufferData();
			ps->CopyAllBufferData();
			DXCore::Get()->context->DrawIndexed((UINT)sky.sky->space_mesh->indexCount, (UINT)sky.sky->space_mesh->indexOffset, (INT)sky.sky->space_mesh->vertexOffset);
			
			UnprepareMaterial(sky.sky->space_material, vs, nullptr, nullptr, nullptr, ps);
		}
		vs->SetInt(SimpleShaderKeys::IS_SPACE, 0);
		ps->SetInt(SimpleShaderKeys::IS_SPACE, 0);
		ps->SetInt(SimpleShaderKeys::CLOUD_TEST, cloud_test);
		ps->SetFloat("cloud_density", sky.sky->cloud_density);
		ps->SetMatrix4x4("spot_view", sky.sky->dir_light->GetSpotMatrix());
		SetEntityLights(&scene_lighting, directional_lights, point_lights);
		PrepareLights(vs, nullptr, nullptr, nullptr, ps);
		PrepareMaterial(sky.mat->data, vs, nullptr, nullptr, nullptr, ps);
		PrepareEntity(sky, vs, nullptr, nullptr, nullptr, ps);
		
		vs->CopyAllBufferData();
		ps->CopyAllBufferData();
		Mesh* mesh = sky.mesh;
		DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);		
		UnprepareEntity(sky, vs, nullptr, nullptr, nullptr, ps);
		UnprepareMaterial(sky.mat->data, vs, nullptr, nullptr, nullptr, ps);
		UnprepareLights(vs, nullptr, nullptr, nullptr, ps);
		ps->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
		context->OMSetBlendState(dxcore->no_blend, NULL, ~0U);
	}
}

void RenderSystem::CheckSceneVisibility(RenderTree& tree) {
	CameraEntity& cam_entity = cameras.GetData()[0];
	DXCore* dxcore = DXCore::Get();
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();

	for (auto& shaders : tree) {
		for (auto& mat : shaders.second) {
			for (auto& de : mat.second.second.GetData()) {
				de.base->scene_visible = IsVisible(cam_entity.camera->world_position, de, cam_entity.camera->xm_view_projection, w, h);
			}
		}
	}
}

void RenderSystem::DrawParticles(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, RenderParticleTree& tree) {

	ID3D11DeviceContext* context = dxcore->context;
	
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);

	SimpleVertexShader* vs = nullptr;
	SimpleHullShader* hs = nullptr;
	SimpleDomainShader* ds = nullptr;
	SimplePixelShader* ps = nullptr;
	SimpleGeometryShader* gs = nullptr;

	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];

	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	float time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;
	context->OMSetBlendState(dxcore->blend, NULL, ~0U);
	//context->RSSetState(dxcore->wireframe_rasterizer);
	context->OMSetDepthStencilState(dxcore->transparent_depth, 1);
	for (auto& shaders : tree) {

		SimpleVertexShader* new_vs = std::get<SHADER_KEY_VS>(shaders.first);
		if (vs != new_vs) {
			vs = new_vs;
			if (vs) {
				vs->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				vs->SetFloat3(CAMERA_DIRECTION, cam_entity.camera->direction);
				vs->SetInt(TESS_ENABLED, this->tess_enabled);
				vs->SetFloat(TIME, time);
				vs->SetShader();
			}
			else {
				context->VSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleHullShader* new_hs = std::get<SHADER_KEY_HS>(shaders.first);
		if (hs != new_hs) {
			hs = new_hs;
			if (hs) {
				hs->SetFloat(TIME, time);
				hs->SetShader();
			}
			else {
				context->HSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleDomainShader* new_ds = std::get<SHADER_KEY_DS>(shaders.first);
		if (ds != new_ds) {
			ds = new_ds;
			if (ds) {
				ds->SetFloat(TIME, time);
				ds->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				ds->SetMatrix4x4(VIEW, cam_entity.camera->view);
				ds->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				ds->SetShader();
			}
			else {
				context->DSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleGeometryShader* new_gs = std::get<SHADER_KEY_GS>(shaders.first);
		if (gs != new_gs) {
			if (gs) {
				gs->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
			}
			gs = new_gs;
			if (gs) {
				gs->SetFloat(TIME, time);
				gs->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
				gs->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				gs->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
				gs->SetInt(SCREEN_W, w);
				gs->SetInt(SCREEN_H, h);
				gs->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				gs->SetMatrix4x4(VIEW, cam_entity.camera->view);
				gs->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				gs->SetFloat(TIME, time);
				gs->SetShader();
				gs->CopyAllBufferData();
			}
			else {
				context->GSSetShader(nullptr, nullptr, 0);
			}
		}

		SimplePixelShader* new_ps = std::get<SHADER_KEY_PS>(shaders.first);
		if (ps != new_ps) {
			if (ps) {
				ps->SetShaderResourceView(DEPTH_TEXTURE, nullptr);				
			}
			ps = new_ps;
			if (ps) {
				ps->SetFloat(TIME, time);
				ps->SetInt(SCREEN_W, w);
				ps->SetInt(SCREEN_H, h);
				ps->SetMatrix4x4(VIEW, cam_entity.camera->view);
				ps->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				ps->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				ps->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
				ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				ps->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());				
				ps->SetShader();
			}
			else {
				context->PSSetShader(nullptr, nullptr, 0);
			}
		}
		ShaderKey sk{ vs, hs, ds, gs, ps };
		Event e(this, EVENT_ID_PREPARE_SHADER);
		e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, sk);
		coordinator->SendEvent(e);
		SetEntityLights(&scene_lighting, directional_lights, point_lights);
		PrepareLights(vs, hs, ds, gs, ps);
		for (auto& mat : shaders.second) {
			if (!mat.second.second.GetData().empty()) {
				PrepareMaterial(mat.first, vs, hs, ds, gs, ps);
				ps->CopyAllBufferData();
				for (auto& de : mat.second.second.GetData()) {
					if (de.base->scene_visible ||de.base->draw_method == DRAW_ALWAYS) {
						for (auto& p : de.particles->data.GetData()) {
							auto vb = p->GetVertexBuffer();
							if (p->visible && p->GetMaterial() == mat.first && vb->GetVertexCount() > 0) {
								p->PrepareParticle(vs, hs, ds, gs, ps);								
								vb->SetBuffers();
								DXCore::Get()->context->Draw((UINT)vb->GetVertexCount(), (UINT)0);
								p->UnprepareParticle(vs, hs, ds, gs, ps);
							}
						}
					}
				}
				UnprepareMaterial(mat.first, vs, hs, ds, gs, ps);
			}
		}
		UnprepareLights(vs, hs, ds, gs, ps);
		e.SetType(EVENT_ID_UNPREPARE_SHADER);
		coordinator->SendEvent(e);
	}
	if (ps != nullptr) {
		ps->SetShaderResourceView(DEPTH_TEXTURE, nullptr);		
	}
	if (gs != nullptr) {
		gs->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
	}
	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->RSSetState(dxcore->drawing_rasterizer);
	context->OMSetDepthStencilState(dxcore->normal_depth, 1);
	DXCore::Get()->context->OMSetBlendState(dxcore->no_blend, NULL, ~0U);
	vertex_buffer->SetBuffers();
}


void RenderSystem::DrawScene(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection,
	                         ID3D11ShaderResourceView* prev_pass_texture, Core::IRenderTarget* target, RenderTree& tree) {
	int draw_count = 0;
	int total_count = 0;
	ID3D11DeviceContext* context = dxcore->context;
	//Render scene to target texture, can be the final backbuffer render
	//or a post process texture pipeline
	ID3D11RenderTargetView* light_target = light_map.RenderTarget();
	ID3D11RenderTargetView* bloom_target = bloom_map.RenderTarget();
	ID3D11DepthStencilView* depth_target_view = depth_target->DepthView();
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);
	if (prev_pass_texture != nullptr) {
		//Render the previous texture in the new target
		//after copying, we render the entities that belongs
		//to the second pass over that texture
		ID3D11RenderTargetView* rv[1] = { target->RenderTarget() };
		context->OMSetRenderTargets(1, rv, nullptr);
		context->RSSetViewports(1, &dxcore->viewport);
		context->RSSetState(dxcore->drawing_rasterizer);
		SimpleVertexShader* vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso");
		SimplePixelShader* ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("PostCopyPS.cso");
		vs->SetShader();
		ps->SetShader();
		ps->SetInt(SCREEN_W, w);
		ps->SetInt(SCREEN_H, h);
		ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		ps->SetShaderResourceView("renderTexture", prev_pass_texture);
		ps->CopyAllBufferData();
		ScreenDraw::Get()->Draw();
		ps->SetShaderResourceView("renderTexture", nullptr);
		vertex_buffer->SetBuffers();
	}

	ID3D11RenderTargetView* rv[7] = { target->RenderTarget(),
		                              light_target,
		                              bloom_target,
		                              rt_ray_sources0.RenderTarget(),
		                              rt_ray_sources1.RenderTarget(),
	                                  position_map.RenderTarget(),
									  prev_position_map.RenderTarget() };

	context->OMSetRenderTargets(7, rv, depth_target_view);
	context->RSSetViewports(1, &dxcore->viewport);
	if (wireframe_enabled) {
		context->RSSetState(dxcore->wireframe_rasterizer);
	}
	else {
		context->RSSetState(dxcore->drawing_rasterizer);
	}

	float speed = 1.0f;
	SkyEntity* sky = nullptr;
	if (!skies.GetData().empty()) {
		sky = &(skies.GetData()[0]);
		speed = sky->sky->second_speed;
	}

	SimpleVertexShader* vs = nullptr; 
	SimpleHullShader* hs = nullptr; 
	SimpleDomainShader* ds = nullptr; 
	SimplePixelShader* ps = nullptr; 
	SimpleGeometryShader* gs = nullptr; 

	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];
	time = ((float)Scheduler::Get()->GetElapsedNanoSeconds() * speed) / 1000000000.0f;

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	for (auto &shaders : tree) {

		SimpleVertexShader* new_vs = std::get<SHADER_KEY_VS>(shaders.first);
		if (vs != new_vs) {
			vs = new_vs;
			if (vs) {
				vs->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				vs->SetFloat3(CAMERA_DIRECTION, cam_entity.camera->direction);
				vs->SetInt(TESS_ENABLED, this->tess_enabled);
				vs->SetFloat(TIME, time);
				vs->SetShader();
			}
			else {
				context->VSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleHullShader* new_hs = std::get<SHADER_KEY_HS>(shaders.first);
		if (hs != new_hs) {
			hs = new_hs;
			if (hs) {
				hs->SetFloat(TIME, time);
				hs->SetShader();
			}
			else {
				context->HSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleDomainShader* new_ds = std::get<SHADER_KEY_DS>(shaders.first);
		if (ds != new_ds) {
			ds = new_ds;
			if (ds) {
				ds->SetFloat(TIME, time);
				ds->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				ds->SetMatrix4x4(VIEW, cam_entity.camera->view);
				ds->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				ds->SetShader();
			}
			else {
				context->DSSetShader(nullptr, nullptr, 0);
			}
		}
		SimpleGeometryShader* new_gs = std::get<SHADER_KEY_GS>(shaders.first);
		if (gs != new_gs) {
			if (gs) {
				gs->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
			}
			gs = new_gs;
			if (gs) {
				gs->SetFloat(TIME, time);
				gs->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
				gs->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				gs->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
				gs->SetInt(SCREEN_W, w);
				gs->SetInt(SCREEN_H, h);
				gs->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				gs->SetMatrix4x4(VIEW, cam_entity.camera->view);
				gs->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				gs->SetFloat(TIME, time);
				gs->SetShader();
			}
			else {
				context->GSSetShader(nullptr, nullptr, 0);
			}
		}

		SimplePixelShader* new_ps = std::get<SHADER_KEY_PS>(shaders.first);
		if (ps != new_ps) {
			if (ps) {
				ps->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
				if (prev_pass_texture != nullptr) {
					ps->SetShaderResourceView("renderTexture", nullptr);
				}
			}
			ps = new_ps;
			if (ps) {
				float3 dir;
				XMStoreFloat3(&dir, cam_entity.camera->xm_direction);
				ps->SetFloat(TIME, time);
				ps->SetInt(SCREEN_W, w);
				ps->SetInt(SCREEN_H, h);
				ps->SetMatrix4x4(VIEW, cam_entity.camera->view);
				ps->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
				ps->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
				ps->SetFloat3("cameraDirection", dir);
				ps->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
				ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
				ps->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
				if (prev_pass_texture != nullptr) {
					ps->SetShaderResourceView("renderTexture", prev_pass_texture);					
				}
				if (sky != nullptr) {
					ps->SetFloat("cloud_density", sky->sky->cloud_density);
					ps->SetMatrix4x4("spot_view", sky->sky->dir_light->GetSpotMatrix());
				}
				ps->SetShader();
			}
			else {
				context->PSSetShader(nullptr, nullptr, 0);
			}
		}
		ShaderKey sk{ vs, hs, ds, gs, ps };
		Event e(this, EVENT_ID_PREPARE_SHADER);
		e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, sk);
		coordinator->SendEvent(e);
		SetEntityLights(&scene_lighting, directional_lights, point_lights);
		PrepareLights(vs, hs, ds, gs, ps);

		for (auto &mat: shaders.second) {
			if (!mat.second.second.GetData().empty()) {
				PrepareMaterial(mat.first, vs, hs, ds, gs, ps);
				if (mat.second.first->multi_material.multi_texture_count > 0) {
					PrepareMultiMaterial(mat.second.first, vs, hs, ds, gs, ps);
					ds->CopyAllBufferData();
				}
				for (auto& de : mat.second.second.GetData()) {
					PrepareEntity(de, vs, hs, ds, gs, ps);
					if (de.base->visible && de.base->scene_visible) {	
 						Mesh* mesh = de.mesh;
						DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);
						draw_count++;
					}
					UnprepareEntity(de, vs, hs, ds, gs, ps);
					total_count++;
				}
				if (mat.second.first->multi_material.multi_texture_count > 0) {
					UnprepareMultiMaterial(mat.second.first, vs, hs, ds, gs, ps);
				}
				UnprepareMaterial(mat.first, vs, hs, ds, gs, ps);
			}
		}
		UnprepareLights(vs, hs, ds, gs, ps);

		e.SetType(EVENT_ID_UNPREPARE_SHADER);
		coordinator->SendEvent(e);
	}
	if (ps != nullptr) {
		ps->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
		if (prev_pass_texture != nullptr) {
			ps->SetShaderResourceView("renderTexture", nullptr);
		}
	}
	if (gs != nullptr) {
		gs->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
	}
	
	if (prev_pass_texture != nullptr) {
		//DrawParticles(w, h, camera_position, view, projection, particle_tree);
	}

	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);
	context->RSSetState(dxcore->drawing_rasterizer);

	ID3D11RenderTargetView* rv_zero[1] = { nullptr };
	context->OMSetRenderTargets(1, rv_zero, nullptr);
	//Print stats
	static int n = 0;
	if (n++ % 500 == 0) {
		printf("Rendered %d / %d objects\n", draw_count, total_count);
	}
}

void RenderSystem::ProcessAntiAlias() {
	if (post_process_pipeline == nullptr) {
		return;
	}

	ID3D11UnorderedAccessView* image = motion_blur_map.UAV();

	int32_t  groupsX = (int32_t)(ceil((float)motion_blur_map.Width() / 32.0f));
	int32_t  groupsY = (int32_t)(ceil((float)motion_blur_map.Height() / 32.0f));

	aa_shader->SetShaderResourceView("depthTexture", depth_map.SRV());
	aa_shader->SetShaderResourceView("normalTexture", rt_ray_sources1.SRV());
	aa_shader->SetShaderResourceView("input", temp_map.SRV());
	aa_shader->SetUnorderedAccessView("output", image);
	aa_shader->CopyAllBufferData();
	aa_shader->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	aa_shader->SetUnorderedAccessView("output", nullptr);
	aa_shader->SetShaderResourceView("depthTexture", nullptr);
	aa_shader->SetShaderResourceView("normalTexture", nullptr);
	aa_shader->SetShaderResourceView("input", nullptr);
	aa_shader->CopyAllBufferData();
}

void RenderSystem::ProcessMix() {
	if (post_process_pipeline == nullptr) {
		return;
	}

	ID3D11UnorderedAccessView* image = temp_map.UAV();

	int32_t  groupsX = (int32_t)(ceil((float)temp_map.Width() / 32.0f));
	int32_t  groupsY = (int32_t)(ceil((float)temp_map.Height() / 32.0f));

	mixer_shader->SetInt("frame_count", frame_count);

	mixer_shader->SetShaderResourceView("depthTexture", depth_map.SRV());
	mixer_shader->SetShaderResourceView("lightTexture", light_map.SRV());
	mixer_shader->SetShaderResourceView("volLightTexture", vol_light_map.SRV());
	mixer_shader->SetShaderResourceView("bloomTexture", bloom_map.SRV());
	mixer_shader->SetShaderResourceView("dustTexture", dust_render_map.SRV());
	mixer_shader->SetShaderResourceView("lensFlareTexture", lens_flare_map.SRV());
	mixer_shader->SetShaderResourceView("rtTexture0", rt_texture_out[0].SRV());
	mixer_shader->SetShaderResourceView("rtTexture1", rt_texture_out[1].SRV());
	mixer_shader->SetShaderResourceView("input", post_process_pipeline->RenderResource());
	mixer_shader->SetUnorderedAccessView("output", image);
	mixer_shader->CopyAllBufferData();
	mixer_shader->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	mixer_shader->SetUnorderedAccessView("output", nullptr);
	mixer_shader->SetShaderResourceView("depthTexture", nullptr);
	mixer_shader->SetShaderResourceView("lightTexture", nullptr);
	mixer_shader->SetShaderResourceView("bloomTexture", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture0", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture1", nullptr);
	mixer_shader->SetShaderResourceView("volLightTexture", nullptr);
	mixer_shader->SetShaderResourceView("dustTexture", nullptr);
	mixer_shader->SetShaderResourceView("input", nullptr);
	mixer_shader->SetShaderResourceView("lensFlareTexture", nullptr);
	mixer_shader->CopyAllBufferData();
}

void RenderSystem::ProcessMotionBlur() {
	if (post_process_pipeline == nullptr) {
		return;
	}
	if (cameras.GetData().empty()) {
		return;
	}
	
	CameraEntity& cam_entity = cameras.GetData()[0];
	int32_t  groupsX = (int32_t)(ceil((float)post_process_pipeline->GetW() / 32.0f));
	int32_t  groupsY = (int32_t)(ceil((float)post_process_pipeline->GetH() / 32.0f));
	motion_blur->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
	motion_blur->SetMatrix4x4("prev_view_proj", cam_entity.camera->prev_view_projection);
	motion_blur->SetShaderResourceView("input", motion_blur_map.SRV());
	motion_blur->SetUnorderedAccessView("output", post_process_pipeline->RenderUAV());
	motion_blur->SetShaderResourceView("motionTexture", motion_texture.SRV());
	motion_blur->CopyAllBufferData();
	motion_blur->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	motion_blur->SetShaderResourceView("input", nullptr);
	motion_blur->SetUnorderedAccessView("output", nullptr);
	motion_blur->SetShaderResourceView("motionTexture", nullptr);
	motion_blur->CopyAllBufferData();
}

void RenderSystem::ProcessDust() {
	if (dust_enabled && dust_map.UAV() != nullptr) {
		int w = dxcore->GetWidth();
		int h = dxcore->GetHeight();
		assert(!cameras.GetData().empty() && "No cameras found");
		CameraEntity& cam_entity = cameras.GetData()[0];
		time = ((float)Scheduler::Get()->GetElapsedNanoSeconds()) / 1000000000.0f;
		float3 dir;
		XMStoreFloat3(&dir, cam_entity.camera->xm_direction);

		int32_t  groupsX = (int32_t)(ceil((float)dust_map.Width() / (32.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)dust_map.Height() / (32.0f)));

		if (!is_dust_init) {
			//Update dust positions
			dust_init->SetFloat(TIME, time);
			dust_init->SetFloat3("range", dust_area);
			dust_init->SetFloat3("offset", dust_offset);
			dust_init->SetUnorderedAccessView("dustTexture", dust_map.UAV());
			dust_init->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
			dust_init->CopyAllBufferData();
			dust_init->SetShader();
			dxcore->context->Dispatch(groupsX, groupsY, 1);
			dust_init->SetUnorderedAccessView("dustTexture", nullptr);
			dust_init->SetShaderResourceView("rgbaNoise", nullptr);
			is_dust_init = true;
		}

		//Update dust position
		dust_update->SetFloat(TIME, time);
		dust_update->SetFloat3("speed", { 1.0f, 0.1f, 1.0f });
		dust_update->SetUnorderedAccessView("dustTexture", dust_map.UAV());
		dust_update->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		dust_update->CopyAllBufferData();
		dust_update->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		dust_update->SetUnorderedAccessView("dustTexture", nullptr);
		dust_update->SetShaderResourceView("rgbaNoise", nullptr);

		dust_render_map.Clear(zero);
		//Render dust
		dust_render->SetFloat(TIME, time);
		dust_render->SetInt(SCREEN_W, w);
		dust_render->SetInt(SCREEN_H, h);
		dust_render->SetMatrix4x4(VIEW, cam_entity.camera->view);
		dust_render->SetMatrix4x4("inverse_view", cam_entity.camera->inverse_view);
		dust_render->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		dust_render->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		dust_render->SetFloat3("cameraDirection", dir);
		dust_render->SetFloat("focusZ", dof_effect ? dof_effect->GetFocus() : -1.0f);
		dust_render->SetFloat("amplitude", dof_effect ? dof_effect->GetAmplitude() : 0.0f);
		dust_render->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		dust_render->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		PrepareLights(dust_render);
		dust_render->SetUnorderedAccessView("dustTexture", dust_map.UAV());
		dust_render->SetUnorderedAccessView("output", dust_render_map.UAV());
		dust_render->SetUnorderedAccessView("depthTextureUAV", depth_map.UAV());
		dust_render->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		dust_render->SetShaderResourceView("vol_data", vol_data.SRV());
		dust_render->CopyAllBufferData();
		dust_render->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		dust_render->SetUnorderedAccessView("dustTexture", nullptr);
		dust_render->SetUnorderedAccessView("output", nullptr);
		dust_render->SetUnorderedAccessView("depthTextureUAV", nullptr);
		dust_render->SetShaderResourceView("rgbaNoise", nullptr);
		dust_render->SetShaderResourceView("vol_data", nullptr);
		UnprepareLights(dust_render);
	}
}

void RenderSystem::ProcessLensFlare() {
	if (lens_flare_enabled && lens_flare_map.UAV() != nullptr) {
		lens_flare_map.Clear(zero);
		
		int32_t  groupsX = (int32_t)(ceil((float)lens_flare_map.Width() / (32.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)lens_flare_map.Height() / (32.0f)));
		
		int w = dxcore->GetWidth();
		int h = dxcore->GetHeight();
		assert(!cameras.GetData().empty() && "No cameras found");
		CameraEntity& cam_entity = cameras.GetData()[0];
		time = ((float)Scheduler::Get()->GetElapsedNanoSeconds()) / 1000000000.0f;
		float3 dir;
		XMStoreFloat3(&dir, cam_entity.camera->xm_direction);

		//Render lens flare effect
		lens_flare->SetFloat(TIME, time);
		lens_flare->SetInt(SCREEN_W, w);
		lens_flare->SetInt(SCREEN_H, h);
		lens_flare->SetMatrix4x4(VIEW, cam_entity.camera->view);
		lens_flare->SetFloat("focusZ", dof_effect ? dof_effect->GetFocus() : -1.0f);
		lens_flare->SetFloat("amplitude", dof_effect ? dof_effect->GetAmplitude() : -1.0f);
		lens_flare->SetMatrix4x4("inverse_view", cam_entity.camera->inverse_view);
		lens_flare->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		lens_flare->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		lens_flare->SetFloat3("cameraDirection", dir);
		lens_flare->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		lens_flare->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		PrepareLights(lens_flare);
		lens_flare->SetUnorderedAccessView("output", lens_flare_map.UAV());
		lens_flare->SetUnorderedAccessView("depthTextureUAV", depth_map.UAV());
		lens_flare->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		lens_flare->SetShaderResourceView("vol_data", vol_data.SRV());
		lens_flare->CopyAllBufferData();
		lens_flare->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		lens_flare->SetUnorderedAccessView("output", nullptr);
		lens_flare->SetUnorderedAccessView("depthTextureUAV", nullptr);
		lens_flare->SetShaderResourceView("rgbaNoise", nullptr);
		lens_flare->SetShaderResourceView("vol_data", nullptr);
		UnprepareLights(lens_flare);
	}
}

void RenderSystem::ProcessMotion() {
	if (!cameras.GetData().empty()) {
		CameraEntity& cam_entity = cameras.GetData()[0];
		int32_t  groupsX = (int32_t)(ceil((float)motion_texture.Width() / 32.0f));
		int32_t  groupsY = (int32_t)(ceil((float)motion_texture.Height() / 32.0f));
		motion_shader->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
		motion_shader->SetMatrix4x4("prev_view_proj", cam_entity.camera->prev_view_projection);
		motion_shader->SetUnorderedAccessView("output", motion_texture.UAV());
		motion_shader->SetShaderResourceView("positionTexture", position_map.SRV());
		motion_shader->SetShaderResourceView("prevPositionTexture", prev_position_map.SRV());
		motion_shader->CopyAllBufferData();
		motion_shader->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		motion_shader->SetUnorderedAccessView("output", nullptr);
		motion_shader->SetShaderResourceView("positionTexture", nullptr);
		motion_shader->SetShaderResourceView("prevPositionTexture", nullptr);
		motion_shader->CopyAllBufferData();
	}
}

void RenderSystem::ProcessRT() {

	if (rt_enabled && bvh_buffer != nullptr) {
		auto& device = dxcore->device;
		ObjectInfo objects[MAX_OBJECTS]{};
		MaterialProps objectMaterials[MAX_OBJECTS]{};
		ID3D11ShaderResourceView* diffuseTextures[MAX_OBJECTS]{};
		
		CameraEntity& cam_entity = cameras.GetData()[0];

		struct Node {
			ObjectInfo obj;
			MaterialProps mat;
			ID3D11ShaderResourceView* diff;
		};

		bool enabled_layer[2]{false};
		auto fill = [op = &enabled_layer[0], tr = &enabled_layer[1], ca = &cam_entity](RenderTree& tree, std::map<float, Node>& distance_map) {
			for (auto& shaders : tree) {
				for (auto& mat : shaders.second) {
					for (auto& de : mat.second.second.GetData()) {
						if (de.base->visible && de.mesh->GetData()->skeletons.empty()) {
							float distance = LENGHT_F3(de.transform->position - ca->camera->world_position);
							
							
							MaterialProps mo = de.mat->data->props;
							ID3D11ShaderResourceView* diffuse_text = de.mat->data->diffuse;

							ObjectInfo o;

							// Get the center and extents of the oriented box
							const float3& orientedBoxCenter = de.bounds->final_box.Center;
							const float3& orientedBoxExtents = de.bounds->final_box.Extents;

							// Calculate the minimum and maximum points of the AABB
							o.aabb_min = { orientedBoxCenter.x - orientedBoxExtents.x,
											orientedBoxCenter.y - orientedBoxExtents.y,
											orientedBoxCenter.z - orientedBoxExtents.z };

							o.aabb_max = { orientedBoxCenter.x + orientedBoxExtents.x,
											orientedBoxCenter.y + orientedBoxExtents.y,
											orientedBoxCenter.z + orientedBoxExtents.z };

							o.vertex_offset = (uint32_t)de.mesh->GetData()->vertexOffset;
							o.index_offset = (uint32_t)de.mesh->GetData()->indexOffset;
							o.object_offset = (uint32_t)de.mesh->GetData()->bvhOffset;

							o.position = orientedBoxCenter;
							o.world = de.transform->world_matrix;
							o.inv_world = de.transform->world_inv_matrix;
							
							o.density = de.mat->data->props.density;
							o.opacity = de.mat->data->props.opacity;

							if (o.opacity > 0.0f) {
								*op = true;
							}
							if (o.opacity < 1.0f) {
								*tr = true;
							}
							distance_map[distance] = { o, mo, diffuse_text };
						}
					}
				}
			}
			};

		auto sort = [=](const std::map<float, Node>& distance_map, ObjectInfo objects[MAX_OBJECTS],
			MaterialProps objectMaterials[MAX_OBJECTS],
			ID3D11ShaderResourceView* diffuseTextures[MAX_OBJECTS],
			int& len) {
				len = 0;
				for (auto& o : distance_map) {
					objects[len] = o.second.obj;
					objectMaterials[len] = o.second.mat;
					diffuseTextures[len++] = o.second.diff;
					if (len >= MAX_OBJECTS) {
						break;
					}
				}			
		};

		std::map<float, Node> distance_map;
		
		ID3D11RenderTargetView* nullRenderTargetViews[1] = { nullptr };
		dxcore->context->OMSetRenderTargets(1, nullRenderTargetViews, nullptr);

		fill(render_tree, distance_map);
		fill(render_pass2_tree, distance_map);
		int len = 0;
		sort(distance_map, objects, objectMaterials, diffuseTextures, len);

		//tbvh.Load(objects, len);
		//tbvh_buffer.Refresh(tbvh.Root(), 0, tbvh.Size());
		rt_texture_ptr = rt_texture[frame_count % 2];
		rt_texture_prev_ptr = rt_texture[1 - frame_count % 2];
		rt_shader->SetInt("nobjects", len);
		rt_shader->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
		rt_shader->SetMatrix4x4("prev_view_proj", cam_entity.camera->prev_view_projection);
		rt_shader->SetInt("frame_count", frame_count);
		rt_shader->SetData("objectMaterials", objectMaterials, len * sizeof(MaterialProps));
		rt_shader->SetData("objectInfos", objects, len * sizeof(ObjectInfo));
		rt_shader->SetUnorderedAccessView("output0", rt_texture_ptr[0].UAV());
		rt_shader->SetUnorderedAccessView("output1", rt_texture_ptr[1].UAV());
		rt_shader->SetUnorderedAccessView("prev_output0", rt_texture_prev_ptr[0].UAV());
		rt_shader->SetUnorderedAccessView("prev_output1", rt_texture_prev_ptr[1].UAV());
		rt_shader->SetShaderResourceView("position_map", position_map.SRV());
		rt_shader->SetShaderResourceView("prev_position_map", prev_position_map.SRV());
		rt_shader->SetShaderResourceView("motionTexture", motion_texture.SRV());
		rt_shader->SetUnorderedAccessView("bloom", bloom_map.UAV());
		rt_shader->SetUnorderedAccessView("props", rt_texture_props.UAV());
		rt_shader->SetUnorderedAccessView("ray0", rt_ray_sources0.UAV());
		rt_shader->SetUnorderedAccessView("ray1", rt_ray_sources1.UAV());

		float3 dir;
		XMStoreFloat3(&dir, cam_entity.camera->xm_direction);
		rt_shader->SetFloat(TIME, time);
		rt_shader->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		rt_shader->SetFloat3("cameraDirection", dir);
		rt_shader->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		rt_shader->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		rt_shader->SetShaderResourceViewArray("DiffuseTextures[0]", diffuseTextures, len);
		
		PrepareLights(rt_shader);

		rt_shader->CopyAllBufferData();

		dxcore->context->CSSetShaderResources(2, 1, bvh_buffer->SRV());
		//dxcore->context->CSSetShaderResources(3, 1, tbvh_buffer.SRV());
		dxcore->context->CSSetShaderResources(4, 1, vertex_buffer->VertexSRV());
		dxcore->context->CSSetShaderResources(5, 1, vertex_buffer->IndexSRV());

		rt_shader->SetShader();		
		int32_t  groupsX = (int32_t)(ceil((float)rt_texture_ptr[0].Width() / (32.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)rt_texture_ptr[0].Height() / (32.0f)));
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		rt_shader->SetUnorderedAccessView("output0", nullptr);
		rt_shader->SetUnorderedAccessView("output1", nullptr);
		rt_shader->SetUnorderedAccessView("prev_output0", nullptr);
		rt_shader->SetUnorderedAccessView("prev_output1", nullptr);
		rt_shader->SetShaderResourceView("position_map", nullptr);
		rt_shader->SetShaderResourceView("prev_position_map", nullptr);
		rt_shader->SetShaderResourceView("motionTexture", nullptr);
		rt_shader->SetUnorderedAccessView("bloom", nullptr);
		rt_shader->SetUnorderedAccessView("props", nullptr);
		rt_shader->SetUnorderedAccessView("ray0", nullptr);
		rt_shader->SetUnorderedAccessView("ray1", nullptr);

		UnprepareLights(rt_shader);
		rt_shader->CopyAllBufferData();

		//Smooth frame
		rt_smooth->SetUnorderedAccessView("props", rt_texture_props.UAV());
		rt_smooth->SetShaderResourceView("depth", depth_map.SRV());
		rt_smooth->SetInt("divider", TEXTURE_RESOLUTION_DIVIDER);
		for (int i = 0; i < 2; ++i) {
			if (enabled_layer[i]) {
				groupsX = (int32_t)(ceil((float)rt_texture_ptr[i].Width() / 32.0f));
				groupsY = (int32_t)(ceil((float)rt_texture_ptr[i].Height() / 32.0f));
				rt_smooth->SetUnorderedAccessView("input", rt_texture_ptr[i].UAV());
				rt_smooth->SetUnorderedAccessView("output", texture_tmp.UAV());
				rt_smooth->SetInt("type", 1);
				rt_smooth->CopyAllBufferData();
				rt_smooth->SetShader();
				dxcore->context->Dispatch(groupsX, groupsY, 1);
				rt_smooth->SetInt("type", 2);
				rt_smooth->SetUnorderedAccessView("input", texture_tmp.UAV());
				rt_smooth->SetUnorderedAccessView("output", rt_texture_out[i].UAV());
				rt_smooth->CopyAllBufferData();
				dxcore->context->Dispatch(groupsX, groupsY, 1);
				rt_smooth->SetUnorderedAccessView("input", nullptr);
				rt_smooth->SetUnorderedAccessView("output", nullptr);
			}
			rt_smooth->SetUnorderedAccessView("props", nullptr);
			rt_smooth->SetShaderResourceView("depth", nullptr);
		}
	}
}

bool RenderSystem::IsVisible(const float3& camera_pos, const DrawableEntity& drawable, const matrix& view_projection, int w, int h) const {
	bool ret = false;
	if (drawable.base->draw_method == DRAW_ALWAYS) {
		return true;
	}

	// Get the center and extents of the oriented box
	const float3& orientedBoxCenter = drawable.bounds->final_box.Center;
	const float3& orientedBoxExtents = drawable.bounds->final_box.Extents;

	// Calculate the minimum and maximum points of the AABB
	float3 aabb_min = { orientedBoxCenter.x - orientedBoxExtents.x,
					orientedBoxCenter.y - orientedBoxExtents.y,
					orientedBoxCenter.z - orientedBoxExtents.z };

	float3 aabb_max = { orientedBoxCenter.x + orientedBoxExtents.x,
					orientedBoxCenter.y + orientedBoxExtents.y,
					orientedBoxCenter.z + orientedBoxExtents.z };

	float3 points[8];
	float3& worldpos = drawable.bounds->final_box.Center;
	float3 dir = { worldpos.x - camera_pos.x, worldpos.y - camera_pos.y, worldpos.z - camera_pos.z };
	float dist2 = abs(DIST2(dir));
	if (dist2 < DIST2(drawable.bounds->final_box.Extents) * 2) {
		return true;
	}

	float box_dist2 = DIST2(drawable.bounds->final_box.Extents);
	drawable.bounds->final_box.GetCorners(points);
	float2 screen_min{ FLT_MAX, FLT_MAX };
	float2 screen_max{ -FLT_MAX, -FLT_MAX };
	bool in_x, in_y;
	for (int i = 0; i < 8; ++i) {
		float2 pscreen = WorldToScreen(points[i], view_projection, (float)w, (float)h);
		screen_min.x = min(screen_min.x, pscreen.x);
		screen_min.y = min(screen_min.y, pscreen.y);
		screen_max.x = max(screen_max.x, pscreen.x);
		screen_max.y = max(screen_max.y, pscreen.y);
		in_x = (screen_min.x >= 0 && screen_min.x <= w) || (screen_max.x >= 0 && screen_max.x <= w) || (screen_min.x <= 0 && screen_max.x >= w);
		in_y = (screen_min.y >= 0 && screen_min.y <= h) || (screen_max.y >= 0 && screen_max.y <= h) || (screen_min.y <= 0 && screen_max.y >= h);
		if (in_x && in_y) {
			return true;
		}
	}
	in_x = (screen_min.x >= 0 && screen_min.x <= w) || (screen_max.x >= 0 && screen_max.x <= w) || (screen_min.x <= 0 && screen_max.x >= w);
	in_y = (screen_min.y >= 0 && screen_min.y <= h) || (screen_max.y >= 0 && screen_max.y <= h) || (screen_min.y <= 0 && screen_max.y >= h);
	return (in_x && in_y);
}

void RenderSystem::PrepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	Event e(this, EVENT_ID_PREPARE_MATERIAL);
	e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, ShaderKey{ vs, hs, ds, gs, ps });
	coordinator->SendEvent(e);
	if (material->props.flags & BLEND_ENABLED_FLAG) {
		DXCore::Get()->context->OMSetBlendState(dxcore->blend, NULL, ~0U);		
	}
	if (vs != nullptr) {
		vs->SetInt(TESS_TYPE, material->tessellation_type);
		vs->SetFloat(TESS_FACTOR, material->tessellation_factor);
	}
	if (ds != nullptr) {
		//Configure shader with material information
		ds->SetShaderResourceView(HIGH_TEXTURE, material->high);
		ds->SetInt(HIGH_TEXTURE_ENABLED, material->high != nullptr);
		ds->SetFloat(DISPLACEMENT_SCALE, material->displacement_scale);		
		ds->CopyAllBufferData();		
	}

	if (ps != nullptr) {
		//Configure shader with material information
		ps->SetData(MATERIAL, &material->props, sizeof(MaterialProps));
		ps->SetShaderResourceView(DIFFUSE_TEXTURE, material->diffuse);
		ps->SetShaderResourceView(NORMAL_TEXTURE, material->normal);
		ps->SetShaderResourceView(SPEC_TEXTURE, material->spec);
		ps->SetShaderResourceView(HIGH_TEXTURE, material->high);
		ps->SetShaderResourceView(AO_TEXTURE, material->ao);
		ps->SetShaderResourceView(ARM_TEXTURE, material->arm);
		ps->SetShaderResourceView(EMISSION_TEXTURE, material->emission);
		ps->SetShaderResourceView(OPACITY_TEXTURE, material->opacity);
		ps->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		ps->SetInt(HIGH_TEXTURE_ENABLED, material->high != nullptr);
	}
}

void RenderSystem::UnprepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	Event e(this, EVENT_ID_UNPREPARE_MATERIAL);
	e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, ShaderKey{ vs, hs, ds, gs, ps });
	coordinator->SendEvent(e);
	
	if (ds != nullptr) {
		ds->SetShaderResourceView(HIGH_TEXTURE, nullptr);
	}
	if (ps != nullptr) {
		ps->SetShaderResourceView(DIFFUSE_TEXTURE, nullptr);
		ps->SetShaderResourceView(NORMAL_TEXTURE, nullptr);
		ps->SetShaderResourceView(HIGH_TEXTURE, nullptr);
		ps->SetShaderResourceView(SPEC_TEXTURE, nullptr);
		ps->SetShaderResourceView(AO_TEXTURE, nullptr);
		ps->SetShaderResourceView(ARM_TEXTURE, nullptr);
		ps->SetShaderResourceView(EMISSION_TEXTURE, nullptr);
		ps->SetShaderResourceView(OPACITY_TEXTURE, nullptr);
		ps->SetShaderResourceView("rgbaNoise", nullptr);
	}
	if (material->props.flags & BLEND_ENABLED_FLAG) {
		DXCore::Get()->context->OMSetBlendState(dxcore->no_blend, NULL, ~0U);
	}
}

void RenderSystem::PrepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs,
	                                    Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                    Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (material->multi_material.multi_texture_count > 0) {
		for (uint32_t i = 0; i < material->multi_material.multi_texture_count; ++i) {
			if (material->multi_material.multi_texture_data[i] != nullptr) {
				multitext_diff[i] = material->multi_material.multi_texture_data[i]->diffuse;
				multitext_norm[i] = material->multi_material.multi_texture_data[i]->normal;
				multitext_spec[i] = material->multi_material.multi_texture_data[i]->spec;
				multitext_ao[i] = material->multi_material.multi_texture_data[i]->ao;
				multitext_arm[i] = material->multi_material.multi_texture_data[i]->arm;
				multitext_disp[i] = material->multi_material.multi_texture_data[i]->high;
				multitext_mask[i] = material->multi_material.multi_texture_mask[i];
			}
		}
	}
	if (vs != nullptr) {
		vs->SetInt(TESS_TYPE, material->multi_material.tessellation_type);
		vs->SetFloat(TESS_FACTOR, material->multi_material.tessellation_factor);
	}

	if (ds != nullptr) {
		ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, material->multi_material.multi_texture_count);
		if (material->multi_material.multi_texture_count > 0) {
			ds->SetData("packed_multi_texture_values", material->multi_material.multi_texture_value.data(), material->multi_material.multi_texture_count * sizeof(float));
			ds->SetData("packed_multi_texture_uv_scales", material->multi_material.multi_texture_uv_scales.data(), material->multi_material.multi_texture_count * sizeof(float));
			ds->SetData("packed_multi_texture_operations", material->multi_material.multi_texture_operation.data(), material->multi_material.multi_texture_count * sizeof(uint32_t));
			ds->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), material->multi_material.multi_texture_count);
			ds->SetFloat(DISPLACEMENT_SCALE, material->multi_material.displacement_scale);
		}
	}
	
	if (ps != nullptr) {
		ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, material->multi_material.multi_texture_count);
		if (material->multi_material.multi_texture_count > 0) {		
			ps->SetFloat("multi_parallax_scale", material->multi_material.multi_parallax_scale);
			ps->SetData("packed_multi_texture_values", material->multi_material.multi_texture_value.data(), material->multi_material.multi_texture_count * sizeof(float));
			ps->SetData("packed_multi_texture_uv_scales", material->multi_material.multi_texture_uv_scales.data(), material->multi_material.multi_texture_count * sizeof(float));
			ps->SetData("packed_multi_texture_operations", material->multi_material.multi_texture_operation.data(), material->multi_material.multi_texture_count * sizeof(uint32_t));
			ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", multitext_diff.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_normalTexture[0]", multitext_norm.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_specularTexture[0]", multitext_spec.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_aoTexture[0]", multitext_ao.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_armTexture[0]", multitext_arm.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_maskTexture[0]", multitext_mask.data(), material->multi_material.multi_texture_count);
		}
	}
}

void RenderSystem::UnprepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs,
	                                      Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                      Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (material->multi_material.multi_texture_count > 0) {
		static ID3D11ShaderResourceView* zero_text[MAX_MULTI_TEXTURE] = {};

		if (ps) {
			ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);
		
			ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_normalTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_specularTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_aoTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_armTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, material->multi_material.multi_texture_count);
			ps->SetShaderResourceViewArray("multi_maskTexture[0]", zero_text, material->multi_material.multi_texture_count);
		}
		if (ds) {
			ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);
			ds->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, material->multi_material.multi_texture_count);
		}
	}
}

void RenderSystem::PrepareLights(Core::ISimpleShader* s) {
	if (!ambient_lights.GetData().empty()) {
		s->SetData(AMBIENT_LIGHT, &ambient_lights.GetData()[0].light->GetData(), sizeof(AmbientLight::Data));
	}
	s->SetInt(DIRLIGHT_COUNT, (int)scene_lighting.dir_lights.size());
	if (!scene_lighting.dir_lights.empty()) {
		s->SetData(DIR_LIGHTS, scene_lighting.dir_lights.data(), (int)(sizeof(DirectionalLight::Data) * scene_lighting.dir_lights.size()));
	}
	s->SetInt(POINT_LIGHT_COUNT, (int)scene_lighting.point_lights.size());
	if (!scene_lighting.point_lights.empty()) {
		s->SetData(POINT_LIGHTS, scene_lighting.point_lights.data(), (int)(sizeof(PointLight::Data) * scene_lighting.point_lights.size()));
	}
	if (!scene_lighting.shadows_perspectives.empty()) {
		s->SetData(LIGHT_PERSPECTIVE_VALUES, scene_lighting.shadows_perspectives.data(), (int)(sizeof(float2) * scene_lighting.shadows_perspectives.size()));
		s->SetShaderResourceViewArray(POINT_SHADOW_MAP_TEXTURE, scene_lighting.shadows.data(), (int)(scene_lighting.shadows.size()));
	}
	if (!scene_lighting.dir_shadows.empty()) {
		s->SetData(DIR_PERSPECTIVE_VALUES, scene_lighting.dir_shadows_perspectives.data(), (int)(sizeof(float4x4) * scene_lighting.dir_shadows_perspectives.size()));
		s->SetShaderResourceViewArray(DIR_SHADOW_MAP_TEXTURE, scene_lighting.dir_shadows.data(), (int)(scene_lighting.dir_shadows.size()));
	}
}

void RenderSystem::UnprepareLights(Core::ISimpleShader* s) {
	ID3D11ShaderResourceView* no_data[MAX_LIGHTS] = {};
	if (!scene_lighting.shadows.empty()) {
		s->SetShaderResourceViewArray(POINT_SHADOW_MAP_TEXTURE, no_data, MAX_LIGHTS);
	}
	if (!scene_lighting.dir_shadows.empty()) {
		s->SetShaderResourceViewArray(DIR_SHADOW_MAP_TEXTURE, no_data, MAX_LIGHTS);
	}
}

void RenderSystem::PrepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (ps != nullptr) {
		PrepareLights(ps);
	}
}

void RenderSystem::UnprepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (ps != nullptr) {
		UnprepareLights(ps);
	}
}

void RenderSystem::PrepareVolumetricShader(Core::ISimpleShader* s) {
	SkyEntity* sky = nullptr;
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();
	float speed = 1.0f;
	if (!skies.GetData().empty()) {
		sky = &(skies.GetData()[0]);
		speed = sky->sky->second_speed;
	}

	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];
	time = ((float)Scheduler::Get()->GetElapsedNanoSeconds() * speed) / 1000000000.0f;
	float3 dir;
	XMStoreFloat3(&dir, cam_entity.camera->xm_direction);

	s->SetFloat(TIME, time);
	s->SetInt(SCREEN_W, w);
	s->SetInt(SCREEN_H, h);
	s->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
	s->SetFloat3("cameraDirection", dir);
	s->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
	
	if (sky != nullptr) {
		s->SetFloat("cloud_density", sky->sky->cloud_density);
	}
	s->SetShaderResourceView("worldTexture", position_map.SRV());
	PrepareLights(s);
}

void RenderSystem::UnprepareVolumetricShader(Core::ISimpleShader* s) {
	s->SetShaderResourceView("worldTexture", nullptr);
	UnprepareLights(s);
}

void RenderSystem::PrepareEntity(DrawableEntity& entity, SimpleVertexShader* vs, SimpleHullShader* hs, SimpleDomainShader* ds, SimpleGeometryShader* gs, SimplePixelShader* ps) {
	Event e(this, entity.base->id, EVENT_ID_PREPARE_ENTITY);
	e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, ShaderKey{ vs, hs, ds, gs, ps });
	coordinator->SendEvent(e);
	const float4x4& world = entity.transform->world_matrix;
	const float4x4& prev_world = entity.transform->prev_world_matrix;
	if (ds != nullptr) {
		if (entity.mesh->GetData()->displacement_scale > 0.0f) {
			ds->SetFloat(DISPLACEMENT_SCALE, entity.mesh->GetData()->displacement_scale);
			ds->CopyAllBufferData();
		}
	}
	if (vs != nullptr) {
		entity.mesh->Prepare(vs);
		vs->SetMatrix4x4(WORLD, world);
		if (entity.mesh->GetData()->tessellation_type > 0) {
			vs->SetInt(TESS_TYPE, entity.mesh->GetData()->tessellation_type);
			vs->SetFloat(TESS_FACTOR, entity.mesh->GetData()->tessellation_factor);
		}		
		vs->CopyAllBufferData();
	}
	if (gs != nullptr) {
		gs->SetMatrix4x4(WORLD, world);
		gs->CopyAllBufferData();		
	}
	if (ps != nullptr) {
		ps->SetMatrix4x4(WORLD, world);
		ps->SetMatrix4x4(PREV_WORLD, prev_world);
		ID3D11ShaderResourceView* mesh_normal_map = entity.mesh->GetData()->normal_map;
		ps->SetShaderResourceView(MESH_NORMAL_MAP, mesh_normal_map);
		ps->SetInt(MESH_NORMAL_MAP_ENABLE, mesh_normal_map != nullptr);
		ps->CopyAllBufferData();
	}
}

void RenderSystem::UnprepareEntity(DrawableEntity& entity, SimpleVertexShader* vs, SimpleHullShader* hs, SimpleDomainShader* ds, SimpleGeometryShader* gs, SimplePixelShader* ps) {
	Event e(this, entity.base->id, EVENT_ID_UNPREPARE_ENTITY);
	e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, ShaderKey{ vs, hs, ds, gs, ps });
	coordinator->SendEvent(e);
	if (vs != nullptr) {
		entity.mesh->Unprepare(vs);
	}
	if (ps != nullptr) {
		ps->SetShaderResourceView(MESH_NORMAL_MAP, nullptr);
	}	
}

void RenderSystem::SetEntityLights(Lighted* lighted, ECS::EntityVector<DirectionalLightEntity>& dir_lights, ECS::EntityVector<PointLightEntity>& point_lights) {
	lighted->dir_lights.clear();
	lighted->point_lights.clear();
	lighted->shadows.clear();
	lighted->shadows_perspectives.clear();
	lighted->dir_shadows.clear();
	lighted->dir_static_shadows.clear();
	lighted->dir_shadows_perspectives.clear();
	lighted->dir_static_shadows_perspectives.clear();

	lighted->dir_lights.reserve(MAX_LIGHTS);
	lighted->point_lights.reserve(MAX_LIGHTS);
	lighted->shadows.reserve(MAX_LIGHTS);
	lighted->shadows_perspectives.reserve(MAX_LIGHTS);
	lighted->dir_shadows.reserve(MAX_LIGHTS);
	lighted->dir_static_shadows.reserve(MAX_LIGHTS);
	lighted->dir_shadows_perspectives.reserve(MAX_LIGHTS);
	lighted->dir_static_shadows_perspectives.reserve(MAX_LIGHTS);

	for (auto const& l: dir_lights.GetData()) {
		lighted->dir_lights.push_back(l.light->GetData());
		lighted->dir_shadows.push_back(l.light->DepthResource());
		lighted->dir_static_shadows.push_back(l.light->StaticDepthResource());
		lighted->dir_shadows_perspectives.push_back(*l.light->GetViewMatrix());
		lighted->dir_static_shadows_perspectives.push_back(*l.light->GetStaticViewMatrix());
	}
	for (auto const& l : point_lights.GetData()) {
		lighted->point_lights.push_back(l.light->GetData());
		lighted->shadows.push_back(l.light->DepthResource());
		lighted->shadows_perspectives.push_back({ l.light->GetLightPerspectiveValues().m[2][2] , l.light->GetLightPerspectiveValues().m[3][2] });
	}
}

void RenderSystem::Clear(const float color[4]) {
	dxcore->ClearScreen(color);
	static const float max_depth[4] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	depth_map.Clear(max_depth);
	depth_view.Clear();
	position_map.Clear(zero);
	prev_position_map.Clear(zero);
	light_map.Clear(color);
	bloom_map.Clear(color);
	temp_map.Clear(color);
	first_pass_texture.Clear(color);	
	texture_tmp.Clear(zero);
	if (post_process_pipeline != nullptr) {
		post_process_pipeline->Clear(color);
	}
}

void RenderSystem::SetPostProcessPipeline(Core::PostProcess* pipeline) {
	if (pipeline != nullptr) {
		if (render_pass2_tree.empty()) {
			first_pass_target = pipeline;
			second_pass_target = nullptr;			
		}
		else {
			first_pass_target = &first_pass_texture;
			second_pass_target = pipeline;
		}
		if (pipeline->DepthResource() != nullptr) {
			depth_target = pipeline;
		}
		else {
			depth_target = dxcore;
		}
		post_process_pipeline = pipeline;
		PostProcess* last = pipeline;
		pipeline->SetShaderResourceView(LIGHT_TEXTURE, light_map.SRV());
		pipeline->SetShaderResourceView("volLightTexture", vol_light_map.SRV());
		pipeline->SetShaderResourceView("bloomTexture", bloom_map.SRV());
		pipeline->SetShaderResourceView("dustTexture", dust_render_map.SRV());
		pipeline->SetShaderResourceView("lensFlareTexture", lens_flare_map.SRV());
		pipeline->SetShaderResourceView("rtTexture0", rt_texture_out[0].SRV());
		pipeline->SetShaderResourceView("rtTexture1", rt_texture_out[1].SRV());
		pipeline->SetShaderResourceView("motionBlur", motion_blur_map.SRV());

		while (last->GetNext() != nullptr) {
			BaseDOFProcess* tmp = dynamic_cast<BaseDOFProcess*>(last);
			if (tmp != nullptr) {
				dof_effect = tmp;
			}
			last = last->GetNext();
		}
		last->SetTarget(dxcore, dxcore);
	}
	else {
		first_pass_target = dxcore;
		depth_target = dxcore;
	}
}

void RenderSystem::Update() {
	static const float back_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	vertex_buffer->SetBuffers();
	Clear(back_color);
	mutex.lock();
	Draw();
	mutex.unlock();
	dxcore->Present();
}

void RenderSystem::PostProcessLight() {

	if (!cameras.GetData().empty()) {

		vol_data.Clear(zero);
		CameraEntity& cam_entity = cameras.GetData()[0];
		PrepareVolumetricShader(vol_shader);
		vol_shader->SetMatrix4x4("view_inverse", cam_entity.camera->inverse_view);
		vol_shader->SetMatrix4x4("projection_inverse", cam_entity.camera->inverse_projection);
		vol_shader->SetUnorderedAccessView("output", vol_light_map.UAV());
		vol_shader->SetUnorderedAccessView("vol_data", vol_data.UAV());
		vol_shader->CopyAllBufferData();
		vol_shader->SetShader();
		int32_t  groupsX = (int32_t)(ceil((float)vol_light_map.Width() / (32.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)vol_light_map.Height() / (32.0f)));
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		vol_shader->SetUnorderedAccessView("output", nullptr);
		vol_shader->SetUnorderedAccessView("vol_data", nullptr);
		UnprepareVolumetricShader(vol_shader);

		//Smooth frame
		blur_shader->SetUnorderedAccessView("input", vol_light_map.UAV());
		blur_shader->SetUnorderedAccessView("output", vol_light_map2.UAV());
		blur_shader->SetShaderResourceView("vol_data", vol_data.SRV());

		groupsX = (int32_t)(ceil((float)vol_light_map.Width() / 32.0f));
		groupsY = (int32_t)(ceil((float)vol_light_map.Height() / 32.0f));
		blur_shader->SetFloat("variance", 5.0f);
		blur_shader->SetInt("type", 1);
		blur_shader->CopyAllBufferData();
		blur_shader->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		blur_shader->SetInt("type", 2);
		blur_shader->SetUnorderedAccessView("input", nullptr);
		blur_shader->SetUnorderedAccessView("output", nullptr);
		blur_shader->SetUnorderedAccessView("input", vol_light_map2.UAV());
		blur_shader->SetUnorderedAccessView("output", vol_light_map.UAV());
		blur_shader->CopyAllBufferData();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		blur_shader->SetUnorderedAccessView("input", nullptr);
		blur_shader->SetUnorderedAccessView("output", nullptr);
		blur_shader->SetShaderResourceView("vol_data", nullptr);
	}

	ID3D11DeviceContext* context = DXCore::Get()->context;
	ID3D11RenderTargetView* rv[1] = { temp_map.RenderTarget() };
	context->OMSetRenderTargets(1, rv, nullptr);
	context->RSSetViewports(1, &dxcore->viewport);
	context->RSSetState(dxcore->drawing_rasterizer);
	SimpleVertexShader* vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso");
	SimplePixelShader* ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("PostBlur.cso");
	vs->SetShader();
	ps->SetShader();
	ps->SetInt(SCREEN_W, bloom_map.Width());
	ps->SetInt(SCREEN_H, bloom_map.Height());
	ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
	ps->SetShaderResourceView("renderTexture", bloom_map.SRV());
	ps->SetInt("horizontal", 1);
	ps->CopyAllBufferData();
	ScreenDraw::Get()->Draw();
	ps->SetShaderResourceView("renderTexture", nullptr);
	ps->CopyAllBufferData();
	rv[0] = { bloom_map.RenderTarget() };
	context->OMSetRenderTargets(1, rv, nullptr);
	ps->SetInt("horizontal", 0);
	ps->SetShaderResourceView("renderTexture", temp_map.SRV());
	ps->CopyAllBufferData();
	ScreenDraw::Get()->Draw();
	ps->SetShaderResourceView("renderTexture", nullptr);
	rv[0] = { nullptr };
	context->OMSetRenderTargets(1, rv, nullptr);
	ProcessDust();
	ProcessLensFlare();
}

void RenderSystem::Draw() {
	DXCore* dxcore = DXCore::Get();
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();
	++frame_count;

	if (!cameras.GetData().empty()) {
		CameraEntity& cam_entity = cameras.GetData()[0];

		matrix view = XMMatrixTranspose(XMLoadFloat4x4(&cam_entity.camera->view));
		matrix projection = XMMatrixTranspose(XMLoadFloat4x4(&cam_entity.camera->projection));
		float3 camera_position = cam_entity.camera->world_position;

		CheckSceneVisibility(render_tree);
		static int count = 0;
		//Only one every 100 frames we refresh static shadows (directional light can change location due to sky component)
		//but this is fine to just make the overhead of casting shadow of static objects almost zero (cost reduced by /STATIC_SHADOW_REFRESH_PERIOD)
		//if ((count++ % STATIC_SHADOW_REFRESH_PERIOD) == 0) {
		//	CastShadows(w, h, camera_position, view, projection, true);
		//}		
		rt_ray_sources0.Clear(zero);
		rt_ray_sources1.Clear(zero);
		CastShadows(w, h, camera_position, view, projection, false);
		DrawDepth(w, h, camera_position, view, projection);
		DrawSky(w, h, camera_position, view, projection);
		DrawScene(w, h, camera_position, view, projection, nullptr, first_pass_target, render_tree);
		if (second_pass_target != nullptr) {
			CheckSceneVisibility(render_pass2_tree);
			DrawScene(w, h, camera_position, view, projection, first_pass_texture.SRV(), second_pass_target, render_pass2_tree);
		}
		ProcessMotion();
		ProcessRT();
		PostProcessLight();
		ProcessMix();
		ProcessAntiAlias();
		ProcessMotionBlur();

		if (post_process_pipeline != nullptr) {
			post_process_pipeline->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
			post_process_pipeline->SetView(*(cam_entity.camera));
		}
		
	}
	if (post_process_pipeline != nullptr) {
		DXCore::Get()->context->RSSetViewports(1, &dxcore->viewport);
		coordinator->SendEvent(this, EVENT_ID_PREPARE_POST);
		post_process_pipeline->Process();
		coordinator->SendEvent(this, EVENT_ID_UNPREPARE_POST);
	}
}

void RenderSystem::EnableTessellation(bool enabled) {
	tess_enabled = enabled;
}
	
bool RenderSystem::IsEnabledTessellation() const {
	return tess_enabled;
}
	
void RenderSystem::EnableNormalMaterialMapping(bool enabled) {
	normal_material_map = enabled;
}
	
bool RenderSystem::IsEnabledEnableNormalMaterialMapping() const {
	return normal_material_map;
}
	
void RenderSystem::EnableNormalMeshMapping(bool enabled) {
	normal_mesh_map;
}

bool RenderSystem::IsEnabledEnableNormalMeshMapping() const {
	return normal_mesh_map;
}

void RenderSystem::SetWireframe(bool enabled) {
	wireframe_enabled = enabled;
}

bool RenderSystem::GetWireframe() const {
	return wireframe_enabled;
}

void RenderSystem::SetCloudTest(bool enabled) {
	cloud_test = enabled;
}

bool RenderSystem::GetCloudTest() const {
	return cloud_test;
}

void RenderSystem::SetRayTracing(bool enabled) {
	rt_enabled = enabled;
	for (int x = 0; x < 2; ++x) {
		for (int i = 0; i < RT_NTEXTURES; ++i) {
			rt_texture[x][i].Clear(zero);
			rt_texture_out[i].Clear(zero);
		}
	}
	light_map.Clear(zero);
	bloom_map.Clear(zero);
}

bool RenderSystem::GetRayTracing() const {
	return rt_enabled;
}

void RenderSystem::SetDustEnabled(bool enabled) {
	dust_enabled = enabled;
}

bool RenderSystem::GetDustEnabled() const {
	return dust_enabled;
}

void RenderSystem::SetDustEffectArea(int32_t num_particles, const float3& area, const float3& offset) {
	dust_area = area;
	dust_offset = offset;
	int particles_texture_size = ((((int32_t)sqrt((float)num_particles) + 31) / 32) * 32); //Make it multiple of 32 to perfect fit computer shaders
	if (dust_map.Width() != particles_texture_size) {
		is_dust_init = false;
		dust_map.Release();
		if (FAILED(dust_map.Init(particles_texture_size, particles_texture_size, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("dust_map.Init failed");
		}
	}
}

void RenderSystem::SetLensFlare(bool enabled) {
	lens_flare_enabled = enabled;
}

bool RenderSystem::GetLensFlare() const {
	return lens_flare_enabled;
}




