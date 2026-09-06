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
static const float ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float minus_one[4] = { -1.0f, -1.0f, -1.0f, -1.0f };


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
const std::string RenderSystem::AUTOFOCUS_TEXTURE = "autofocusTexture";
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

	//No Mesh, no Material, no Bounds: a splat cloud brings its own geometry and its
	//own shading, and is drawn by DrawSplats rather than by any of the render trees.
	splat_signature.set(coordinator->GetComponentType<Base>(), true);
	splat_signature.set(coordinator->GetComponentType<Transform>(), true);
	splat_signature.set(coordinator->GetComponentType<SplatCloud>(), true);
}

void RenderSystem::OnEntityDestroyed(ECS::Entity entity) {
	ambient_lights.Remove(entity);
	point_lights.Remove(entity);
	directional_lights.Remove(entity);
	cameras.Remove(entity);
	splat_clouds.Remove(entity);

	RemoveDrawable(entity, render_pass2_tree);
	RemoveDrawable(entity, render_tree);
	RemoveDrawable(entity, depth_tree);
	RemoveDrawable(entity, shadow_tree);
	RemoveParticle(entity, particle_tree);
	drawables.Remove(entity);
}

void RenderSystem::RefreshDrawable(ECS::Entity entity) {
	std::scoped_lock l(mutex);
	//Remove first, then let the normal registration path put it back under whatever
	//keys its components now produce.
	RemoveDrawable(entity, render_pass2_tree);
	RemoveDrawable(entity, render_tree);
	RemoveDrawable(entity, depth_tree);
	RemoveDrawable(entity, shadow_tree);
	OnEntitySignatureChanged(entity, coordinator->GetEntitySignature(entity));
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

	if ((entity_signature & splat_signature) == splat_signature)
	{
		splat_clouds.Insert(entity, SplatEntity{ coordinator, entity });
	}
	else
	{
		splat_clouds.Remove(entity);
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
		drawables.Insert(entity, drawable);
	}
	else {
		RemoveDrawable(entity, render_pass2_tree);
		RemoveDrawable(entity, render_tree);
		RemoveDrawable(entity, depth_tree);
		RemoveDrawable(entity, shadow_tree);
		drawables.Remove(entity);
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
		int w = dxcore->GetWidth();
		int h = dxcore->GetHeight();
		
		for (int i = 0; i < 2; ++i)
		{
			if (FAILED(light_map[i].Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
				throw std::exception("light_map.Init failed");
			}
		}
		if (FAILED(vol_light_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("vol_light_map.Init failed");
		}
		if (FAILED(lens_flare_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("lens_flare_map.Init failed");
		}
		if (FAILED(dust_render_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("dust_render_map.Init failed");
		}
		if (FAILED(vol_light_map2.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("vol_light_map2.Init failed");
		}
		//UNORDERED_ACCESS because the Gaussian splat rasterizer (SplatRasterCS) writes
		//this from compute. It stays an RTV in DrawScene's seven-target set - the flag
		//only adds a second way to bind it, and the two are never bound at once.
		if (FAILED(position_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT,
			nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("position_map.Init failed");
		}
		//UNORDERED_ACCESS for the same reason position_map has it: SplatRasterCS fills the
		//whole G-buffer from compute, and a cloud with no previous position would report
		//zero motion however fast it moved.
		if (FAILED(prev_position_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT,
			nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("prev_position_map.Init failed");
		}
		if (FAILED(bloom_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("bloom_map.Init failed");
		}
		if (FAILED(temp_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("temp_map.Init failed");
		}
		if (FAILED(depth_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("depth_map.Init failed");
		}
		if (FAILED(depth_view.Init(w, h))) {
			throw std::exception("depth_view.Init failed");
		}
		if (FAILED(first_pass_texture.Init(w, h))) {
			throw std::exception("first_pass_texture.Init failed");
		}
		if (FAILED(motion_texture.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("motion_texture.Init failed");
		}
		if (FAILED(rgba_noise_texture.Init(RGA_NOISE_W, RGBA_NOISE_H, DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, (const uint8_t*)rgba_noise_map, RGBA_NOISE_LEN))) {
			throw std::exception("rgba_noise_texture.Init failed");
		}

		if (FAILED(motion_blur_map.Init(w, h, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
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
		//A single texel holding the autofocus distance. R32_FLOAT because the
		//shader reads the previous frame's value back out of the UAV to smooth
		//against it, and typed UAV loads are only guaranteed for the 32 bit formats.
		if (FAILED(autofocus_map.Init(1, 1, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("autofocus_map.Init failed");
		}
		autofocus_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("AutoFocusCS.cso");
		if (autofocus_shader == nullptr) {
			throw std::exception("autofocus shader.Init failed");
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
		splat_preprocess = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatPreprocessCS.cso");
		if (splat_preprocess == nullptr) {
			throw std::exception("splat_preprocess shader.Init failed");
		}
		splat_bin = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatBinCS.cso");
		if (splat_bin == nullptr) {
			throw std::exception("splat_bin shader.Init failed");
		}
		splat_compact = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatCompactCS.cso");
	if (splat_compact == nullptr) {
		throw std::exception("splat_compact shader.Init failed");
	}
	splat_scan = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatScanCS.cso");
		if (splat_scan == nullptr) {
			throw std::exception("splat_scan shader.Init failed");
		}
		splat_base = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatBaseCS.cso");
		if (splat_base == nullptr) {
			throw std::exception("splat_base shader.Init failed");
		}
		splat_raster = ShaderFactory::Get()->GetShader<SimpleComputeShader>("SplatRasterCS.cso");
		if (splat_raster == nullptr) {
			throw std::exception("splat_raster shader.Init failed");
		}
		rt_di_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("RayTraceCS.cso");
		if (rt_di_shader == nullptr) {
			throw std::exception("raytrace shader.Init failed");
		}
		gi_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("GIRayTraceCS.cso");
		if (gi_shader == nullptr) {
			throw std::exception("GIRayTraceCS shader.Init failed");
		}
		vol_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("VolumetricLightCS.cso");
		if (vol_shader == nullptr) {
			throw std::exception("VolumetricLightCS shader.Init failed");
		}
		blur_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("BlurCS.cso");
		if (blur_shader == nullptr) {
			throw std::exception("BlurCS shader.Init failed");
		}
		rt_di_denoiser = ShaderFactory::Get()->GetShader<SimpleComputeShader>("DenoiserCS.cso");
		if (rt_di_denoiser == nullptr) {
			throw std::exception("DenoiserCS shader.Init failed");
		}
		gi_average = ShaderFactory::Get()->GetShader<SimpleComputeShader>("GIAverageCS.cso");
		if (gi_average == nullptr) {
			throw std::exception("GIAverageCS shader.Init failed");
		}
		gi_weights = ShaderFactory::Get()->GetShader<SimpleComputeShader>("GICalcWeightsCS.cso");
		if (gi_weights == nullptr) {
			throw std::exception("RestirCalcWeightsCS shader.Init failed");
		}
		rcache_resolve = ShaderFactory::Get()->GetShader<SimpleComputeShader>("RadianceCacheResolveCS.cso");
		if (rcache_resolve == nullptr) {
			throw std::exception("RadianceCacheResolveCS shader.Init failed");
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
		copy_texture = ShaderFactory::Get()->GetShader<SimpleComputeShader>("CopyTexturesCS.cso");
		if (copy_texture == nullptr) {
			throw std::exception("Copy textures shader.Init failed");
		}

		LoadRTResources();

		rt_thread = std::thread([&]() {
			std::unique_lock<std::mutex> l(rt_mutex);
			while (true) {
				rt_signal.wait_for(l, std::chrono::seconds(3), [&] { return rt_end || rt_prepare; });

				if (rt_prepare) {
					PrepareRT();
					rt_prepare = false;
				}
				
				if (rt_end) {
					break;
				}
			}
		});
	}
	catch (std::exception& e) {
		printf("RenderSystem::Init: ERROR: %s\n", e.what());
		ret = false;
	}
	return ret;
}

RenderSystem::~RenderSystem() {
	rt_end = true;
	rt_signal.notify_all();
	rt_thread.join();

	depth_view.Release();
	for (int i = 0; i < 2; ++i)
	{
		light_map[i].Release();
	}	
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
	depth_map.Release();
	autofocus_map.Release();

	for (int i = 0; i < RT_NTEXTURES; ++i) {
		for (int x = 0; x < 2; ++x) {
			rt_textures_di[x][i].Release();
		}
	}

	for (int x = 0; x < RT_GI_NTEXTURES; ++x) {
		rt_textures_gi[x].Release();
	}

	rt_textures_gi_tiles.Release();
	for (int x = 0; x < 2; ++x) {
		restir_pdf[x].Release();
	}
	restir_w.Release();
	texture_tmp.Release();

	rt_ray_sources0.Release();
	rt_ray_sources1.Release();
	rcache.Release();
	rcache_value.Release();
	rcache_stats.Release();
	vol_data.Release();
}

void RenderSystem::LoadRTResources() {
	std::lock_guard<std::mutex> lock(rt_mutex);
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();
	for (int x = 0; x < RT_NTEXTURES; ++x) {
		int div = RT_TEXTURE_RESOLUTION_DIVIDER;
		for (int n = 0; n < 2; ++n) {
			rt_textures_di[n][x].Release();
			if (FAILED(rt_textures_di[n][x].Init(w / div, h / div, DXGI_FORMAT::DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
				throw std::exception("rt_texture.Init failed");
			}
		}
	}

	uint32_t restir_div = 3;// max(RT_TEXTURE_RESOLUTION_DIVIDER, 2);

	for (int n = 0; n < RT_GI_NTEXTURES; ++n) {
		rt_textures_gi[n].Release();
		if (FAILED(rt_textures_gi[n].Init(w / restir_div, h / restir_div, DXGI_FORMAT::DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("rt_texture.Init failed");
		}
	}
		
	uint32_t tile_div = min(restir_div, RT_TEXTURE_RESOLUTION_DIVIDER) * RESTIR_KERNEL;
	rt_textures_gi_tiles.Release();
	if (FAILED(rt_textures_gi_tiles.Init(w / tile_div + 1, h/ tile_div + 1, DXGI_FORMAT::DXGI_FORMAT_R8_UINT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
		throw std::exception("rt_texture.Init failed");
	}

	for (int n = 0; n < 2; ++n) {
		restir_pdf[n].Release();
		if (FAILED(restir_pdf[n].Init(w / restir_div, h / restir_div, DXGI_FORMAT_R32G32B32A32_UINT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("restir_pdf.Init failed");
		}
	}
	restir_w.Release();
	if (FAILED(restir_w.Init(w / restir_div, h / restir_div, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
		throw std::exception("restir_w.Init failed");
	}

	texture_tmp.Release();
	if (FAILED(texture_tmp.Init(w / RT_TEXTURE_RESOLUTION_DIVIDER, h / RT_TEXTURE_RESOLUTION_DIVIDER, DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
		throw std::exception("texture_tmp.Init failed");
	}

	//The world radiance cache. Unlike everything above it this is not sized from
	//the screen - it is keyed by where surfaces are, not by which pixel sees them -
	//so a resolution change does not need to rebuild it. It is rebuilt here anyway
	//because LoadRTResources is also what a device reset runs through, and the
	//alternative is a buffer whose lifetime is subtly different from its neighbours'.
	if (FAILED(rcache.Init(RADIANCE_CACHE_ENTRIES * RADIANCE_CACHE_STRIDE))) {
		throw std::exception("rcache.Init failed");
	}
	if (FAILED(rcache_value.Init(RADIANCE_CACHE_ENTRIES * RADIANCE_CACHE_VALUE_STRIDE))) {
		throw std::exception("rcache_value.Init failed");
	}
	if (FAILED(rcache_stats.Init(RADIANCE_CACHE_STATS_BYTES, true))) {
		throw std::exception("rcache_stats.Init failed");
	}
	rcache.Clear(0);
	rcache_value.Clear(0);
	rcache_stats.Clear(0);
	rcache_reset = true;

	ResetRTBBuffers();
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
	context->RSSetViewports(1, &dxcore->viewport);
	//Biased so the depth this pass leaves in depth_view is a conservative occluder for
	//the passes that render on top of it (DrawSky, DrawScene). depth_map, the world
	//distance the effect shaders read, is a pixel shader output and is not biased.
	context->RSSetState(dxcore->depth_rasterizer);
	context->OMSetDepthStencilState(dxcore->normal_depth, 1);

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
		e.SetType(EVENT_ID_DEPTH_UNPREPARE_SHADER);
		coordinator->SendEvent(e);
	}
}

uint64_t RenderSystem::StaticShadowSignature() {
	uint64_t sig = 1469598103934665603ULL;
	auto mix = [&sig](uint64_t v) {
		sig = (sig ^ v) * 1099511628211ULL;
	};
	for (auto& shaders : shadow_tree) {
		for (auto& mat : shaders.second) {
			for (const DrawableEntity& de : mat.second.second.GetData()) {
				if (!de.base->is_static || !de.base->cast_shadow || !de.base->visible) {
					continue;
				}
				mix(de.base->id);
				//Position only: a static caster that is being moved is what we need to
				//notice, and float bits make the compare exact without an epsilon.
				const float3& p = de.transform->position;
				mix(*reinterpret_cast<const uint32_t*>(&p.x));
				mix(*reinterpret_cast<const uint32_t*>(&p.y));
				mix(*reinterpret_cast<const uint32_t*>(&p.z));
			}
		}
	}
	return sig;
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
					if (static_shadows) {
						//Static casters go into one plain map fitted to the widest
						//cascade, so this pass is a single view like the pre-cascade one.
						gs->SetInt(ACTIVE_VIEWS, 0x01);
						gs->SetMatrix4x4(CUBE_VIEW_0, *l.light->GetStaticViewMatrix());
					}
					else {
						//One pass fills every cascade: the shadow geometry shader is the
						//same one the point light's six cube faces go through, so routing
						//a triangle to slice N via SV_RenderTargetArrayIndex costs nothing
						//new. The alternative - a draw pass per cascade - would re-walk
						//the whole shadow tree N times to no benefit, since a directional
						//caster is not culled per cascade anyway.
						const float4x4* cascades = l.light->GetViewMatrix();
						const int cascade_count = l.light->GetCascadeCount();
						gs->SetInt(ACTIVE_VIEWS, (1 << cascade_count) - 1);
						static_assert(MAX_SHADOW_CASCADES <= 6,
							"ShadowMapCubeGS.hlsl carries six view matrices; add slots "
							"there and entries below before raising MAX_SHADOW_CASCADES.");
						static const std::string* const CASCADE_VIEW[] = {
							&CUBE_VIEW_0, &CUBE_VIEW_1, &CUBE_VIEW_2, &CUBE_VIEW_3,
							&CUBE_VIEW_4, &CUBE_VIEW_5
						};
						for (int c = 0; c < cascade_count && c < MAX_SHADOW_CASCADES; ++c) {
							gs->SetMatrix4x4(*CASCADE_VIEW[c], cascades[c]);
						}
					}
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
		//The sky covers the whole screen and is drawn before the scene, so most of what
		//it shades is about to be painted over. Binding the depth pre-pass buffer lets
		//the hardware reject those pixels before the (expensive, cloud-marching) pixel
		//shader runs. Depth writes off: the sky is blended in behind everything and the
		//buffer belongs to the pre-pass. The dome sits beyond the far plane and the
		//sky rasterizer has depth clip off, so where nothing occludes it its depth
		//clamps to the cleared 1.0 and LESS_EQUAL lets it through.
		ID3D11RenderTargetView* rv[2] = { first_pass_target->RenderTarget(), current_light_map->RenderTarget() };
		context->OMSetRenderTargets(2, rv, depth_view.Depth());
		context->OMSetBlendState(dxcore->blend, NULL, ~0U);
		context->OMSetDepthStencilState(dxcore->depth_prepass_read, 1);
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
		ID3D11RenderTargetView* rv_zero[2] = { nullptr, nullptr };
		context->OMSetRenderTargets(2, rv_zero, nullptr);
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

void RenderSystem::SelectLods(const Components::Camera& camera) {
	//The frustum is read off the projection rather than assumed, so the coverage a
	//model is judged by tracks the camera's actual field of view - the same reason
	//the shadow cascade fit reads it from there.
	float tan_half_h = 0.0f, tan_half_v = 0.0f, near_z = 0.0f, far_z = 0.0f;
	if (!camera.GetFrustumParams(tan_half_h, tan_half_v, near_z, far_z)) {
		return;
	}
	const float3& camera_position = camera.world_position;
	for (DrawableEntity& de : drawables.GetData()) {
		Mesh* mesh = de.mesh;
		MeshData* data = mesh->GetData();
		if (data == nullptr || data->lods.size() < 2) {
			continue;
		}
		if (!mesh->lod_enabled) {
			if (mesh->current_lod != 0) {
				mesh->SetLod(0);
			}
			continue;
		}
		//The bounding sphere of the entity's world box, which is what makes this a
		//handful of arithmetic instead of eight projections: it is rotation
		//invariant, so a model turning on the spot keeps one coverage and does not
		//switch level for spinning. The screen-rect of the projected corners would be
		//tighter and would also need every corner tested for being behind the camera.
		const float3& center = de.bounds->final_box.Center;
		const float3& extents = de.bounds->final_box.Extents;
		const float3 to_center{ center.x - camera_position.x,
								center.y - camera_position.y,
								center.z - camera_position.z };
		const float radius = sqrtf(DIST2(extents));
		const float distance = sqrtf(DIST2(to_center));
		//Depth along the view axis, not distance: the screen is a fixed angle wide,
		//so how big something draws depends on how far *in front* it is. Distance
		//would shrink everything towards the edges of the view, which is where a
		//model is most likely to be moving.
		const float depth = XMVectorGetX(XMVector3Dot(
			XMVectorSet(to_center.x, to_center.y, to_center.z, 0.0f), camera.xm_direction));
		float coverage = 1.0f;
		//Anything reaching the near plane is treated as filling the screen. Its
		//projection is unbounded there, and a model the camera is inside of is the
		//last one that should be losing detail.
		if (depth > near_z + radius) {
			//The viewport is 2*tan*depth across at that depth, so the sphere covers
			//pi*r^2 of (2*tan_h*depth)*(2*tan_v*depth).
			coverage = (XM_PI * radius * radius) /
				(4.0f * tan_half_h * tan_half_v * depth * depth);
			if (coverage > 1.0f) {
				coverage = 1.0f;
			}
		}
		mesh->SetLod(data->SelectLod(coverage, distance, mesh->current_lod));
	}
}

void RenderSystem::LatchPreviousFrame(const Components::Camera& camera) {
	//What this frame was drawn with becomes what the next one measures its motion against.
	//All three of these have to be latched here, once per rendered frame, and nowhere else:
	//the systems that produce them (CameraSystem, StaticMeshSystem/PhysicsSystem,
	//Mesh::Update) all run on the background thread on timers of their own, so a latch
	//there either erases the motion outright - every tick in which nothing moved copied the
	//current value over the previous one - or measures one of *its* ticks rather than one
	//frame.
	prev_view_projection = camera.view_projection;
	for (DrawableEntity& de : drawables.GetData()) {
		de.transform->prev_world_matrix = de.transform->world_matrix;
		//The pose as well as the place: a rig animating on the spot keeps one world matrix
		//all frame, so its skinning matrices are the only record that anything moved.
		de.mesh->LatchPrevJoints();
	}
	//Splat clouds separately, because `drawables` is built from the render trees and a
	//cloud is not in them - it has no Mesh and no Material, and DrawSplats draws it. Left
	//out, its prev_world_matrix keeps the zero it was constructed with, and a zero matrix
	//is not merely a stale pose: SplatRasterCS reconstructs the previous position through
	//inverse(world) * prev_world, which then sends every point to w = 0. The cloud reports
	//full-strength motion while standing still, and every temporal pass downstream
	//reprojects it to nowhere.
	//
	//An entity carrying both a Mesh and a SplatCloud is latched by both loops, which is
	//the same assignment twice.
	for (SplatEntity& se : splat_clouds.GetData()) {
		se.transform->prev_world_matrix = se.transform->world_matrix;
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
	ID3D11RenderTargetView* light_target = current_light_map->RenderTarget();
	ID3D11RenderTargetView* bloom_target = bloom_map.RenderTarget();
	//Render on top of the depth pre-pass buffer rather than into an empty one. Every
	//opaque surface in the frame is already in it, so the hardware rejects occluded
	//fragments before the pixel shader runs instead of after - this pass shades one of
	//the heaviest shaders in the engine (all lights, shadows, parallax) and the scene
	//is drawn in material order, not front to back, so without it a pixel can be
	//fully shaded several times over. The rasterizer culls nothing (CULL_NONE), which
	//alone means every closed mesh shades its far side too.
	ID3D11DepthStencilView* depth_target_view = depth_view.Depth();
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
		ps->SetShaderResourceView("lightTexture", current_light_map->SRV());
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
									  prev_position_map.RenderTarget(),
									  position_map.RenderTarget() };

	context->OMSetRenderTargets(7, rv, depth_target_view);
	//Depth writes stay on: what the pre-pass skipped (alpha-tested and blended
	//materials, and the second-pass tree, which is not in depth_tree at all) still has
	//to occlude itself and whatever comes after it.
	context->OMSetDepthStencilState(dxcore->depth_prepass, 1);
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
				if (prev_light_map != nullptr) {
					ps->SetShaderResourceView("prevLightTexture", prev_light_map->SRV());
				}				
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
				//The stack belongs to the *material*, not to the representative entity's
				//component: this bucket draws every entity using mat.first with one set
				//of layer constants, so there is only one stack it could ever apply.
				if (mat.first->multi_material != nullptr) {
					PrepareMultiMaterial(mat.first, vs, hs, ds, gs, ps);
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
				if (mat.first->multi_material != nullptr) {
					UnprepareMultiMaterial(mat.first, vs, hs, ds, gs, ps);
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
		ps->SetShaderResourceView("prevLightTexture", nullptr);
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

bool RenderSystem::EnsureSplatBuffers(uint32_t splat_count, uint32_t tiles_x, uint32_t tiles_y) {
	//Grow only. A frame whose largest cloud is smaller than the last one reuses what
	//is there: these are tens of megabytes, and reallocating them because the camera
	//turned away from the big cloud would be a stall for nothing.
	if (splat_count > splat_views_capacity) {
		if (FAILED(splat_views.Init((uint32_t)sizeof(SplatView), splat_count))) {
			LOG_ERROR("RenderSystem::EnsureSplatBuffers: splat_views.Init failed for %u splats (%.1f MB)",
				splat_count, (float)(splat_count * sizeof(SplatView)) / (1024.0f * 1024.0f));
			splat_views.Release();
			splat_views_capacity = 0;
			return false;
		}
		splat_views_capacity = splat_count;
	}
	//A fixed 16 bytes, so it is created once rather than tracked against anything.
	if (splat_stats.SizeBytes() == 0 && FAILED(splat_stats.Init(SPLAT_STATS_BYTES, true))) {
		LOG_ERROR("RenderSystem::EnsureSplatBuffers: splat_stats.Init failed");
		return false;
	}
	//Three uints, and independent of the tile count - created once, like the stats.
	//`true` is the DispatchIndirect flag: SplatCompactCS builds the group count in
	//element 0 with an InterlockedAdd, and the scan and the rasterizer are launched
	//straight off it without the CPU ever seeing the number.
	if (!splat_dispatch_args.IsValid() &&
		FAILED(splat_dispatch_args.Init(3, DXGI_FORMAT_R32_UINT, 4, true))) {
		LOG_ERROR("RenderSystem::EnsureSplatBuffers: splat_dispatch_args.Init failed");
		return false;
	}
	if (tiles_x != splat_tiles_x || tiles_y != splat_tiles_y) {
		const uint32_t tiles = tiles_x * tiles_y;
		//Four of these are one uint per tile and cost nothing - 130 KB at 1080p for the
		//lot. bucket_offsets is the exception and is now the second largest allocation
		//the pass makes: tiles * SPLAT_DEPTH_BUCKETS, which at 1024 buckets is 7.1 MB at
		//1080p and 57 MB at the 2560x1377 the editor runs at. That is the price of the
		//binning being an exact sort rather than a 128-band approximation, and it is
		//paid per resolution rather than per cloud - but it is also cleared once per
		//cloud per frame, so a level with several clouds pays the clear each time.
		//The entry pool below is still the largest, and it is sized by content.
		if (FAILED(splat_tile_depth.Init(tiles)) ||
			FAILED(splat_tile_total.Init(tiles)) ||
			FAILED(splat_tile_base.Init(tiles)) ||
			FAILED(splat_tile_list.Init(tiles)) ||
			FAILED(splat_bucket_offsets.Init(tiles * SPLAT_DEPTH_BUCKETS))) {
			LOG_ERROR("RenderSystem::EnsureSplatBuffers: tile buffer Init failed for %ux%u tiles",
				tiles_x, tiles_y);
			splat_tile_depth.Release();
			splat_tile_total.Release();
			splat_tile_base.Release();
			splat_tile_list.Release();
			splat_bucket_offsets.Release();
			splat_tiles_x = 0;
			splat_tiles_y = 0;
			return false;
		}
		splat_tiles_x = tiles_x;
		splat_tiles_y = tiles_y;
	}

	//The pool. Grown from what the GPU reported it actually needed last time it was
	//asked, and never shrunk - the readback is several frames stale and a cloud the
	//camera is swinging past would otherwise thrash. `wanted` is a floor rather than an
	//exact size: the first frame has no measurement, and a frame that came up short
	//still has to allocate for what it *asked* for, not for what it managed to write.
	//The floor matters more than the per-splat term, and not for the reason it looks
	//like. Entries per splat is not near 1: a splat is binned into every tile its 3
	//sigma ellipse touches, and a *small* cloud close to the camera has the largest
	//ellipses - the automation fixture's 600-splat sphere projects each splat ~68 px
	//across, which is ~72 tiles apiece, or 120 entries per splat. A big capture seen
	//from a normal distance sits under 2. No single multiplier covers both, which is
	//why the measurement below is what actually sizes this and the floor only has to
	//carry a small cloud until the measurement lands.
	uint32_t wanted = max(splat_count, SPLAT_POOL_MIN_ENTRIES);
	if (splat_stats_cpu.total_binned > 0) {
		wanted = max(wanted, (uint32_t)(splat_stats_cpu.total_binned * SPLAT_POOL_MARGIN));
	}
	if (wanted > splat_entries_capacity) {
		if (FAILED(splat_entries.Init(wanted))) {
			LOG_ERROR("RenderSystem::EnsureSplatBuffers: splat_entries.Init failed for %u entries (%.1f MB)",
				wanted, (float)(wanted * sizeof(uint32_t)) / (1024.0f * 1024.0f));
			splat_entries.Release();
			splat_entries_capacity = 0;
			return false;
		}
		splat_entries_capacity = wanted;
	}
	return true;
}

void RenderSystem::DrawSplats(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection) {
	if (splat_clouds.GetData().empty() || splat_preprocess == nullptr ||
		splat_bin == nullptr || splat_scan == nullptr || splat_base == nullptr ||
		splat_raster == nullptr) {
		return;
	}
	//The scene colour this composites into is the post-process chain's own texture -
	//the one DrawScene rendered to and the one ProcessMix reads back as `input`. With
	//no chain installed there is no such texture, exactly as in ProcessMix.
	if (post_process_pipeline == nullptr || current_light_map == nullptr) {
		return;
	}
	ID3D11UnorderedAccessView* scene_uav = post_process_pipeline->RenderUAV();
	if (scene_uav == nullptr) {
		return;
	}
	assert(!cameras.GetData().empty() && "No cameras found");
	CameraEntity& cam_entity = cameras.GetData()[0];

	const uint32_t tiles_x = ((uint32_t)w + SPLAT_TILE_SIZE - 1) / SPLAT_TILE_SIZE;
	const uint32_t tiles_y = ((uint32_t)h + SPLAT_TILE_SIZE - 1) / SPLAT_TILE_SIZE;
	const uint32_t tile_total_count = tiles_x * tiles_y;

	//The scratch buffers hold one cloud at a time, so they are sized for the largest
	//one about to be drawn rather than for the sum. Measured before anything is
	//dispatched: a mid-loop grow would throw away the bins of the cloud in flight.
	uint32_t max_splats = 0;
	for (SplatEntity& e : splat_clouds.GetData()) {
		if (e.base->visible && e.cloud->data != nullptr && e.cloud->data->Prepared()) {
			max_splats = max(max_splats, e.cloud->data->Count());
		}
	}
	if (max_splats == 0 || !EnsureSplatBuffers(max_splats, tiles_x, tiles_y)) {
		return;
	}

	//The near plane the projection actually encodes, rather than a constant that would
	//go stale if the camera's clip planes ever changed.
	float tan_half_h = 0.0f;
	float tan_half_v = 0.0f;
	float near_z = 0.01f;
	float far_z = 1000.0f;
	cam_entity.camera->GetFrustumParams(tan_half_h, tan_half_v, near_z, far_z);

	SkyEntity* sky = skies.GetData().empty() ? nullptr : &(skies.GetData()[0]);
	const float frame_time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;
	ID3D11DeviceContext* context = dxcore->context;

	//A cloud carries no Lighted component to gather per-entity lights from, so it takes
	//the scene's whole light set - the same thing DrawSky does, and refilled here rather
	//than relying on DrawSky having run, since a level need not contain a sky.
	SetEntityLights(&scene_lighting, directional_lights, point_lights);

	//Everything a splat's material is. The rasterizer overwrites specIntensity per
	//pixel (it is a per-cloud constant carried on the SplatView) and clears the flags,
	//since a splat has no maps of any kind; the rest is what the lighting functions
	//read - bloom_scale above all, which at a nonzero value would have splats feeding
	//a bloom buffer this pass does not write.
	MaterialProps splat_material{};
	splat_material.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	splat_material.specIntensity = 0.0f;
	splat_material.bloom_scale = 0.0f;
	splat_material.opacity = 1.0f;
	splat_material.density = 1.0f;
	splat_material.emission = 0.0f;
	//Ray tracing on, and it is the one flag that matters here: the rasterizer clears
	//every texture-map bit but keeps this one, and it decides whether the ray sources a
	//splat pixel writes are ones the tracers follow or ones they skip. Off, a cloud
	//fills the G-buffer and still cannot be seen in a reflection or gathered from by
	//ReSTIR.
	splat_material.flags = RAY_TRACING_ENABLED_FLAG;

	for (SplatEntity& e : splat_clouds.GetData()) {
		if (!e.base->visible || e.cloud->data == nullptr || !e.cloud->data->Prepared()) {
			continue;
		}
		const uint32_t count = e.cloud->data->Count();
		if (count == 0) {
			continue;
		}
		//The window the depth quantization spans: how near and how far this
		//cloud reaches from the camera. Fitted per cloud rather than taken from the
		//camera's clip planes, because 1022 steps spread over 0.01..1000 would put a
		//whole cloud inside one or two of them and the sort would order nothing.
		//
		//Via the cloud's bounding *sphere*, and that is not a shortcut. The obvious fit -
		//min and max over the distances to the eight transformed box corners - gets the
		//far end right and the near end wrong, because the nearest point of a box to a
		//camera outside it is generally on a face, not at a corner. Over-tight is the one
		//failure mode that matters here: the shader saturates, so every splat nearer than
		//the fit quantizes to step 0, the whole front of the cloud ties in the sort, and
		//`nearest` becomes an arbitrary pick that drags surface_depth, the reconstructed
		//world position and therefore the lighting with it. A sphere around the corners
		//is always conservative, and a slightly loose range only costs quantization
		//resolution the 1022 steps have to spare.
		const float3 bmin = e.cloud->data->min_dimensions;
		const float3 bmax = e.cloud->data->max_dimensions;
		const vector3d cam = XMLoadFloat3(&camera_position);
		//world_xmmatrix, NOT world_matrix. The latter is stored transposed, because that
		//is the convention every shader reads `world` under (see SplatCloudSystem's
		//ComposeTransform) - and XMVector3TransformCoord wants the untransposed form, so
		//loading the stored one puts the translation in the wrong place and returns a
		//point unrelated to the cloud. It fails quietly: the fit still produces *a*
		//number, so the depth quantization and the tile cull both go on comparing values
		//that no longer describe anything.
		const matrix cloud_world = e.transform->world_xmmatrix;
		const float3 local_centre{ (bmin.x + bmax.x) * 0.5f,
								   (bmin.y + bmax.y) * 0.5f,
								   (bmin.z + bmax.z) * 0.5f };
		const vector3d centre = XMVector3TransformCoord(XMLoadFloat3(&local_centre), cloud_world);
		float radius = 0.0f;
		for (int c = 0; c < 8; ++c) {
			const float3 corner{ (c & 1) ? bmax.x : bmin.x,
								 (c & 2) ? bmax.y : bmin.y,
								 (c & 4) ? bmax.z : bmin.z };
			const vector3d wp = XMVector3TransformCoord(XMLoadFloat3(&corner), cloud_world);
			radius = max(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(wp, centre))));
		}
		const float centre_dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(centre, cam)));
		//Clamped at zero for a camera inside the cloud, where the nearest splat is
		//underfoot rather than a sphere radius away.
		const float depth_min = max(0.0f, centre_dist - radius);
		const float depth_max = centre_dist + radius;
		//A degenerate range (a single-point cloud) would divide by zero in the shader and
		//quantize everything to one step, which is correct-but-useless rather than wrong -
		//the floor just keeps it finite.
		const float depth_range = max(1e-3f, depth_max - depth_min);
		//The slab: a world thickness, not a share of this cloud's depth - see the note on
		//SPLAT_SLAB_WORLD. Floored at one depth band, because the tail cannot usefully be
		//finer than the granularity the slice is ordered to - the band the surface was
		//declared at the end of is the one holding the rest of that surface's own splats,
		//so a tail shorter than it would cut into the surface being measured. Both passes
		//get the same value.
		const float band_world = depth_range / (float)SPLAT_DEPTH_BUCKETS;
		const float depth_slab = max(SPLAT_SLAB_WORLD, band_world);

		// --- project every Gaussian and bin it into the tiles it covers ---------------
		//Reset per cloud, not per frame: the structure describes one cloud's splats and
		//the rasterizer consumes it before the next cloud refills it. The entry pool is
		//deliberately NOT cleared - every entry a tile's slice contains is written by the
		//scatter, and the pool is far the largest buffer here.
		splat_bucket_offsets.Clear(0);
		//All ones, so the first splat to touch a tile wins the InterlockedMin and a tile
		//nothing touches rejects every splat rather than accepting them all.
		splat_tile_depth.Clear(SPLAT_NO_DEPTH);
		splat_stats.Clear(0);
		//Zeroes the group count SplatCompactCS accumulates into element 0. Y and Z are
		//written by that shader rather than cleared, a single Clear having only one
		//value to give every element.
		splat_dispatch_args.Clear(0);

		const float3 albedo_tint{ e.cloud->albedo_scale, e.cloud->albedo_scale, e.cloud->albedo_scale };
		splat_preprocess->SetMatrix4x4(WORLD, e.transform->world_matrix);
		splat_preprocess->SetMatrix4x4(VIEW, cam_entity.camera->view);
		splat_preprocess->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		splat_preprocess->SetFloat3(CAMERA_POSITION, camera_position);
		splat_preprocess->SetFloat("splat_opacity_scale", e.cloud->opacity_scale);
		splat_preprocess->SetFloat3("albedo_tint", albedo_tint);
		splat_preprocess->SetFloat("splat_spec", e.cloud->spec_intensity);
		splat_preprocess->SetInt("splat_count", (int)count);
		splat_preprocess->SetInt(SCREEN_W, w);
		splat_preprocess->SetInt(SCREEN_H, h);
		splat_preprocess->SetInt("tiles_x", (int)tiles_x);
		splat_preprocess->SetInt("tiles_y", (int)tiles_y);
		splat_preprocess->SetInt("invert_normals", e.cloud->invert_normals ? 1 : 0);
		splat_preprocess->SetFloat("near_plane", near_z);
		splat_preprocess->SetFloat("far_plane", far_z);
		splat_preprocess->SetFloat("depth_quant_min", depth_min);
		splat_preprocess->SetFloat("depth_quant_range", depth_range);
		//Screen-coverage LOD. The cloud's bounding sphere projects to a disc of this many
		//pixels; past `max_density` splats over each of them the extra ones only refine an
		//average that has already converged, so keep that many and drop the rest before
		//they are loaded.
		//
		//One expression covers both jobs the density knob has. It falls with distance
		//because the area does - which is what stops a cloud costing the same 3.4M binned
		//entries at 100 units as at 2 - and it binds at full size for a capture carrying
		//more splats than its silhouette can show, which is how an over-dense model is
		//thinned without re-importing it. Clamped to 1, so a cloud that is not over its
		//density is untouched and this costs one comparison per splat.
		const float screen_radius_px =
			(radius / max(near_z, centre_dist)) / max(1e-4f, tan_half_v) * ((float)h * 0.5f);
		const float area_px = XM_PI * screen_radius_px * screen_radius_px;
		const float keep_prob = (count > 0)
			? min(1.0f, (max(1.0f, e.cloud->max_density) * area_px) / (float)count)
			: 1.0f;
		splat_preprocess->SetFloat("splat_keep_prob", keep_prob);
		//Computed here rather than as a reciprocal per splat, and it is what keeps the
		//LOD from changing how much of a pixel the cloud covers.
		splat_preprocess->SetFloat("splat_alpha_comp", 1.0f / max(1e-6f, keep_prob));
		splat_preprocess->SetFloat("splat_depth_slab", depth_slab);
		splat_preprocess->SetFloat("point_size_scale", e.cloud->point_size_scale);
		splat_preprocess->SetShaderResourceView("splats", *(e.cloud->data->SRV()));
		//The depth pre-pass result, for the coarse reject. Read here and *written* by
		//the rasterizer below, which is why it is unbound before that dispatch rather
		//than left for D3D to unbind with a hazard warning.
		splat_preprocess->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
		splat_preprocess->SetUnorderedAccessView("splat_views", splat_views.UAV());
		splat_preprocess->SetUnorderedAccessView("tile_depth", splat_tile_depth.UAV());
		splat_preprocess->CopyAllBufferData();
		splat_preprocess->SetShader();
		const uint32_t splat_groups = (count + SPLAT_PREPROCESS_GROUP - 1) / SPLAT_PREPROCESS_GROUP;
		context->Dispatch(splat_groups, 1, 1);
		splat_preprocess->SetShaderResourceView("splats", nullptr);
		splat_preprocess->SetShaderResourceView(DEPTH_TEXTURE, nullptr);
		splat_preprocess->SetUnorderedAccessView("splat_views", nullptr);
		splat_preprocess->SetUnorderedAccessView("tile_depth", nullptr);

		// --- compact the covered tiles ------------------------------------------------
		//tile_depth is final now, so which tiles this cloud touches is known, and the two
		//passes that want one group per tile can be launched over just those. One thread
		//per tile - 32 groups at 1080p against the 8160 the scan alone used to take.
		splat_compact->SetInt("tile_count", (int)tile_total_count);
		splat_compact->SetShaderResourceView("tile_depth", splat_tile_depth.SRV());
		splat_compact->SetUnorderedAccessView("tile_list", splat_tile_list.UAV());
		splat_compact->SetUnorderedAccessView("tile_total", splat_tile_total.UAV());
		splat_compact->SetUnorderedAccessView("dispatch_args", splat_dispatch_args.UAV());
		splat_compact->CopyAllBufferData();
		splat_compact->SetShader();
		context->Dispatch((tile_total_count + SPLAT_COMPACT_GROUP - 1) / SPLAT_COMPACT_GROUP, 1, 1);
		splat_compact->SetShaderResourceView("tile_depth", nullptr);
		splat_compact->SetUnorderedAccessView("tile_list", nullptr);
		splat_compact->SetUnorderedAccessView("tile_total", nullptr);
		//Released before the DispatchIndirect calls below: a buffer supplying indirect
		//arguments may not be bound as a UAV at the time it is read as arguments.
		splat_compact->SetUnorderedAccessView("dispatch_args", nullptr);

		// --- bin every splat by its own quantized depth --------------------------------
		//Bucket index IS the quantized depth: SPLAT_DEPTH_BUCKETS is 1024 over a
		//SPLAT_MAX_DEPTH_STEP (1023) quantization, one bucket per step, so binning needs
		//nothing beyond the near/far window every splat's depth is already quantized
		//against - no per-tile window, no separate step-size constant. See the note on
		//SPLAT_DEPTH_BUCKETS in SplatCommon.hlsli for why an earlier, per-tile-relative
		//version of this bought no precision and was removed.
		splat_bin->SetInt("splat_count", (int)count);
		splat_bin->SetInt("tiles_x", (int)tiles_x);
		splat_bin->SetInt("tiles_y", (int)tiles_y);
		splat_bin->SetFloat("depth_quant_min", depth_min);
		splat_bin->SetFloat("depth_quant_range", depth_range);
		splat_bin->SetInt("entry_capacity", (int)splat_entries_capacity);
		splat_bin->SetShaderResourceView("splat_views", splat_views.SRV());
		splat_bin->SetUnorderedAccessView("bucket_offsets", splat_bucket_offsets.UAV());
		splat_bin->SetUnorderedAccessView("splat_stats", splat_stats.UAV());

		//Pass 0: histogram only. tile_base does not exist yet, and splat_entries is not
		//written, so neither is bound.
		splat_bin->SetInt("bin_pass", 0);
		splat_bin->CopyAllBufferData();
		splat_bin->SetShader();
		context->Dispatch(splat_groups, 1, 1);
		splat_bin->SetUnorderedAccessView("bucket_offsets", nullptr);

		// --- turn the histogram into an allocation ------------------------------------
		//Two levels. A tile has only SPLAT_DEPTH_BUCKETS counters, so its scan fits in one
		//group with nothing spilled; the per-tile totals that leaves behind are the only
		//thing that has to be scanned across the whole frame, and there are few enough of
		//those for a single group to do it. That decomposition is what avoids a general
		//multi-block scan with block sums and a third pass.
		splat_scan->SetInt("tile_count", (int)tile_total_count);
		splat_scan->SetShaderResourceView("tile_list", splat_tile_list.SRV());
		splat_scan->SetUnorderedAccessView("bucket_offsets", splat_bucket_offsets.UAV());
		splat_scan->SetUnorderedAccessView("tile_total", splat_tile_total.UAV());
		splat_scan->SetUnorderedAccessView("splat_stats", splat_stats.UAV());
		splat_scan->CopyAllBufferData();
		splat_scan->SetShader();
		//One group per *covered* tile. The count is in the argument buffer and the CPU
		//never learns it - reading it back to size an ordinary Dispatch would cost a
		//pipeline stall every frame, which is the whole reason this is indirect.
		context->DispatchIndirect(splat_dispatch_args.Buffer(), 0);
		splat_scan->SetShaderResourceView("tile_list", nullptr);
		splat_scan->SetUnorderedAccessView("bucket_offsets", nullptr);
		splat_scan->SetUnorderedAccessView("tile_total", nullptr);
		splat_scan->SetUnorderedAccessView("splat_stats", nullptr);

		splat_base->SetInt("tile_count", (int)tile_total_count);
		splat_base->SetShaderResourceView("tile_total", splat_tile_total.SRV());
		splat_base->SetUnorderedAccessView("tile_base", splat_tile_base.UAV());
		splat_base->SetUnorderedAccessView("splat_stats", splat_stats.UAV());
		splat_base->CopyAllBufferData();
		splat_base->SetShader();
		context->Dispatch(1, 1, 1);
		splat_base->SetShaderResourceView("tile_total", nullptr);
		splat_base->SetUnorderedAccessView("tile_base", nullptr);
		splat_base->SetUnorderedAccessView("splat_stats", nullptr);

		//Pass 1: the same enumeration again, writing this time. bucket_offsets now holds
		//each bucket's start within its tile and is used as the cursor.
		//
		//EVERY binding is re-established here, including the two that were already set
		//before pass 0. D3D11 binding slots belong to the *stage*, not to the shader
		//object SimpleShader hangs its API on, so the scan dispatches in between - which
		//bind their own resources at their own t0/t1 and then null them on cleanup -
		//leave this shader's slots holding whatever they last set. Relying on a binding
		//to survive an intervening dispatch is what made the scatter read splat_views as
		//null: every splat came back with radius 0, took the reject path, and the pool
		//was never written at all. Nothing about that is visible except as an empty
		//frame, since the counting pass had already produced correct-looking totals.
		splat_bin->SetInt("bin_pass", 1);
		splat_bin->SetShaderResourceView("splat_views", splat_views.SRV());
		splat_bin->SetShaderResourceView("tile_base", splat_tile_base.SRV());
		splat_bin->SetUnorderedAccessView("bucket_offsets", splat_bucket_offsets.UAV());
		splat_bin->SetUnorderedAccessView("splat_entries", splat_entries.UAV());
		splat_bin->SetUnorderedAccessView("splat_stats", splat_stats.UAV());
		splat_bin->CopyAllBufferData();
		splat_bin->SetShader();
		context->Dispatch(splat_groups, 1, 1);
		splat_bin->SetShaderResourceView("splat_views", nullptr);
		splat_bin->SetShaderResourceView("tile_base", nullptr);
		splat_bin->SetUnorderedAccessView("bucket_offsets", nullptr);
		splat_bin->SetUnorderedAccessView("splat_entries", nullptr);
		splat_bin->SetUnorderedAccessView("splat_stats", nullptr);

		// --- rasterize the bins, lit by the scene's own lights ------------------------
		splat_raster->SetMatrix4x4(WORLD, e.transform->world_matrix);
		splat_raster->SetMatrix4x4(VIEW, cam_entity.camera->view);
		splat_raster->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		splat_raster->SetFloat3(CAMERA_POSITION, camera_position);
		splat_raster->SetFloat3(CAMERA_DIRECTION, cam_entity.camera->direction);
		splat_raster->SetInt(SCREEN_W, w);
		splat_raster->SetInt(SCREEN_H, h);
		splat_raster->SetInt("tiles_x", (int)tiles_x);
		splat_raster->SetInt("tiles_y", (int)tiles_y);
		//The bucket span, the tile near-depths, the slab and the band width are all gone:
		//the walk composites the slice in order and never asks where an entry sits in the
		//depth range. The quantization WINDOW stays, and for a different reason than it
		//used to - the rasterizer re-derives each entry's quantized depth to find which
		//entries are co-located, since a run at one depth has to be composited as a single
		//layer or the result depends on the order the binning atomics happened to produce.
		//It must be the same window SplatBinCS was given a few lines up.
		splat_raster->SetFloat("surface_alpha", e.cloud->surface_alpha);
		splat_raster->SetFloat("splat_quant_min", depth_min);
		splat_raster->SetFloat("splat_quant_range", depth_range);
		//inverse(world) * prev_world, so the rasterizer can take a world position of this
		//cloud back to where that point was last frame without storing a previous position
		//per splat. Built from world_xmmatrix and the *untransposed* previous matrix, then
		//transposed on the way out - the same convention `world` is uploaded under.
		//prev_world_matrix is latched once per frame by LatchPreviousFrame, so on a frame
		//where the cloud has not moved this comes out as the identity and the two position
		//maps agree, which is exactly zero motion.
		const matrix prev_world = XMMatrixTranspose(XMLoadFloat4x4(&e.transform->prev_world_matrix));
		const matrix prev_from_world = XMMatrixMultiply(
			XMMatrixInverse(nullptr, e.transform->world_xmmatrix), prev_world);
		float4x4 prev_from_world_t;
		XMStoreFloat4x4(&prev_from_world_t, XMMatrixTranspose(prev_from_world));
		splat_raster->SetMatrix4x4("prev_world_from_world", prev_from_world_t);
		splat_raster->SetFloat(TIME, frame_time);
		splat_raster->SetData(MATERIAL, &splat_material, sizeof(MaterialProps));
		if (sky != nullptr) {
			splat_raster->SetFloat("cloud_density", sky->sky->cloud_density);
			splat_raster->SetMatrix4x4("spot_view", sky->sky->dir_light->GetSpotMatrix());
		}
		splat_raster->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		splat_raster->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		splat_raster->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		PrepareLights(splat_raster);
		//SRVs, not the UAVs the preprocess wrote them through: nothing here writes them
		//back, and a read-only bind does not serialize against the other UAV users.
		splat_raster->SetShaderResourceView("splat_views", splat_views.SRV());
		splat_raster->SetShaderResourceView("tile_base", splat_tile_base.SRV());
		splat_raster->SetShaderResourceView("tile_total", splat_tile_total.SRV());
		splat_raster->SetShaderResourceView("splat_entries", splat_entries.SRV());
		splat_raster->SetShaderResourceView("tile_list", splat_tile_list.SRV());
		splat_raster->SetUnorderedAccessView("scene_out", scene_uav);
		splat_raster->SetUnorderedAccessView("light_out", current_light_map->UAV());
		splat_raster->SetUnorderedAccessView("depth_out", depth_map.UAV());
		//The rest of the G-buffer, in the same targets DrawScene bound as render targets
		//5/6 and 3/4. Bound as UAVs here and as RTVs there, never both at once.
		splat_raster->SetUnorderedAccessView("rt_ray0_out", rt_ray_sources0.UAV());
		splat_raster->SetUnorderedAccessView("rt_ray1_out", rt_ray_sources1.UAV());
		splat_raster->SetUnorderedAccessView("position_out", position_map.UAV());
		splat_raster->SetUnorderedAccessView("prev_position_out", prev_position_map.UAV());
		splat_raster->SetUnorderedAccessView("splat_stats", splat_stats.UAV());
		//The ninth UAV, and the only one that needs asking permission for: a compute
		//shader may bind more than eight only at feature level 11_1. Left unbound on an
		//11_0 device the shader's writes to it are discarded, so the cloud renders exactly
		//as it did before and the bloom behind it is simply not occluded.
		if (dxcore->SupportsExtendedUAVSlots()) {
			splat_raster->SetUnorderedAccessView("bloom_out", bloom_map.UAV());
		}
		splat_raster->CopyAllBufferData();
		splat_raster->SetShader();
		//One group per *covered* tile, and the group is the tile: SPLAT_TILE_SIZE^2
		//threads, one per pixel of it. Indirect and one-dimensional over the same
		//compacted list and the same argument the scan used - which is why the shader
		//rebuilds its pixel coordinates from the tile index instead of taking them off
		//SV_DispatchThreadID. Before this, a cloud covering four tiles still launched
		//8160 groups, 8156 of them to read tile_total and retire.
		context->DispatchIndirect(splat_dispatch_args.Buffer(), 0);
		splat_raster->SetShaderResourceView("tile_list", nullptr);
		splat_raster->SetShaderResourceView("splat_views", nullptr);
		splat_raster->SetShaderResourceView("tile_base", nullptr);
		splat_raster->SetShaderResourceView("tile_total", nullptr);
		splat_raster->SetShaderResourceView("splat_entries", nullptr);
		splat_raster->SetShaderResourceView("rgbaNoise", nullptr);
		//Released before the next cloud's preprocess reads depth_map as an SRV, and
		//before anything downstream binds these three as inputs.
		splat_raster->SetUnorderedAccessView("scene_out", nullptr);
		splat_raster->SetUnorderedAccessView("light_out", nullptr);
		splat_raster->SetUnorderedAccessView("depth_out", nullptr);
		splat_raster->SetUnorderedAccessView("rt_ray0_out", nullptr);
		splat_raster->SetUnorderedAccessView("rt_ray1_out", nullptr);
		splat_raster->SetUnorderedAccessView("position_out", nullptr);
		splat_raster->SetUnorderedAccessView("prev_position_out", nullptr);
		splat_raster->SetUnorderedAccessView("splat_stats", nullptr);
		if (dxcore->SupportsExtendedUAVSlots()) {
			splat_raster->SetUnorderedAccessView("bloom_out", nullptr);
		}
		UnprepareLights(splat_raster);

		//Never blocks: the copy taken this frame is read several frames from now, and a
		//failed map leaves the previous reading in place - the right answer for counters
		//nothing is gated on. Only while something is asking, for the same reason the
		//radiance cache readback is gated.
		//Unconditional, unlike the radiance cache counters this is otherwise modelled on.
		//`total_binned` is not a diagnostic here - it is what EnsureSplatBuffers sizes the
		//entry pool from - so gating it on something having asked for stats means the pool
		//never grows unless a tool happens to be watching. That is exactly what happened:
		//the automation fixture's cloud rendered at 2.6% of its coverage because nothing
		//in the suite calls splat_info, so the measurement never landed and the pool stayed
		//at its floor. A 32-byte CopyResource and a non-blocking Map, only on frames that
		//actually drew a cloud, is the right price for a value the frame depends on.
		splat_stats_cpu.capacity = splat_entries_capacity;
		uint32_t raw[SPLAT_STATS_BYTES / sizeof(uint32_t)] = {};
		if (splat_stats.Readback(raw, sizeof(raw))) {
			splat_stats_cpu.tiles_used = raw[0];
			splat_stats_cpu.max_per_tile = raw[1];
			splat_stats_cpu.total_binned = raw[2];
			splat_stats_cpu.entries_walked = raw[3];
			splat_stats_cpu.dropped = raw[5];
			splat_stats_cpu.tiles_rastered = raw[6];
			splat_stats_cpu.pixels_written = raw[7];
		}
	}
	context->CSSetShader(nullptr, nullptr, 0);
}

void RenderSystem::ProcessAntiAlias() {
	if (post_process_pipeline == nullptr) {
		return;
	}

	ID3D11UnorderedAccessView* image = motion_blur_map.UAV();

	int32_t  groupsX = (int32_t)(ceil((float)motion_blur_map.Width() / 8.0f));
	int32_t  groupsY = (int32_t)(ceil((float)motion_blur_map.Height() / 8.0f));

	aa_shader->SetShaderResourceView("input", temp_map.SRV());
	aa_shader->SetSamplerState("basicSampler", dxcore->basic_sampler);
	//Anti-aliasing edits the pixels a debug view exists to show, so it is off for
	//as long as one is up (the stored setting is pushed fresh every frame, so it
	//comes straight back).
	aa_shader->SetInt("enabled", aa_enabled && !IsDebugBufferActive());
	aa_shader->SetUnorderedAccessView("output", image);
	aa_shader->CopyAllBufferData();
	aa_shader->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	aa_shader->SetUnorderedAccessView("output", nullptr);
	aa_shader->SetShaderResourceView("input", nullptr);
	aa_shader->CopyAllBufferData();
}

void RenderSystem::ProcessMix() {
	if (post_process_pipeline == nullptr) {
		return;
	}

	ID3D11UnorderedAccessView* image = temp_map.UAV();

	int32_t  groupsX = (int32_t)(ceil((float)temp_map.Width() / 8.0f));
	int32_t  groupsY = (int32_t)(ceil((float)temp_map.Height() / 8.0f));

	mixer_shader->SetInt("frame_count", frame_count);
	mixer_shader->SetFloat("time", time);
	mixer_shader->SetInt("debug", rt_debug);
	mixer_shader->SetFloat("debug_gain", debug_gain);
	mixer_shader->SetInt("rt_enabled", rt_enabled & (rt_quality != eRtQuality::OFF? 0xFF:0x00));
	mixer_shader->SetShaderResourceView("depthTexture", depth_map.SRV());
	mixer_shader->SetShaderResourceView("lightTexture", current_light_map->SRV());
	mixer_shader->SetShaderResourceView("volLightTexture", vol_light_map.SRV());
	mixer_shader->SetShaderResourceView("bloomTexture", bloom_map.SRV());
	mixer_shader->SetShaderResourceView("dustTexture", dust_render_map.SRV());
	mixer_shader->SetShaderResourceView("lensFlareTexture", lens_flare_map.SRV());
	//rt_texture_di_curr is only ever assigned inside ProcessRT, which itself only runs
	//while at least one of reflections/refractions is enabled (rt_enabled &
	//(RT_REFLEX_ENABLE | RT_REFRACT_ENABLE)) - exactly the situation rt_texture_gi_curr
	//is already guarded against below. This was read unconditionally instead, which is
	//harmless once some earlier frame has run ProcessRT (a stale-but-valid pointer from
	//last time reflections were on) but null-derefs on the very first frame of a
	//session that starts with both flags off - e.g. a level whose saved render
	//settings load that way.
	if (rt_texture_di_curr != nullptr) {
		mixer_shader->SetShaderResourceView("emissionTexture", rt_texture_di_curr[RT_TEXTURE_EMISSION].SRV());
		mixer_shader->SetShaderResourceView("rtTexture0", rt_texture_di_curr[RT_TEXTURE_REFLEX].SRV());
		mixer_shader->SetShaderResourceView("rtTexture1", rt_texture_di_curr[RT_TEXTURE_REFRACT].SRV());
	}
	if (rt_texture_gi_curr != nullptr)
	{
		mixer_shader->SetShaderResourceView("rtTexture2", rt_texture_gi_curr->SRV());
	}
	mixer_shader->SetShaderResourceView("positions", rt_ray_sources0.SRV());
	mixer_shader->SetShaderResourceView("normals", rt_ray_sources1.SRV());
	mixer_shader->SetShaderResourceView("motionTexture", motion_texture.SRV());
	mixer_shader->SetShaderResourceView("input", post_process_pipeline->RenderResource());
	//For the two radiance cache views only. Bound every frame rather than behind a
	//test on the debug value: the branch that reads them is uniform and costs
	//nothing when it is not taken, and a resource left unbound between frames is a
	//far more common source of a black debug view than the cost of binding it.
	if (!cameras.GetData().empty()) {
		mixer_shader->SetFloat3(CAMERA_POSITION, cameras.GetData()[0].camera->world_position);
	}
	mixer_shader->SetShaderResourceView("rcache", rcache.SRV());
	mixer_shader->SetShaderResourceView("rcache_value", rcache_value.SRV());
	mixer_shader->SetUnorderedAccessView("output", image);
	mixer_shader->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
	mixer_shader->CopyAllBufferData();
	mixer_shader->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	mixer_shader->SetUnorderedAccessView("output", nullptr);
	mixer_shader->SetShaderResourceView("depthTexture", nullptr);
	mixer_shader->SetShaderResourceView("lightTexture", nullptr);
	mixer_shader->SetShaderResourceView("bloomTexture", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture0", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture1", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture2", nullptr);
	mixer_shader->SetShaderResourceView("rtTexture3", nullptr);
	mixer_shader->SetShaderResourceView("volLightTexture", nullptr);
	mixer_shader->SetShaderResourceView("motionTexture", nullptr);
	mixer_shader->SetShaderResourceView("dustTexture", nullptr);
	mixer_shader->SetShaderResourceView("input", nullptr);
	mixer_shader->SetShaderResourceView("lensFlareTexture", nullptr);
	mixer_shader->SetShaderResourceView("rgbaNoise", nullptr);
	mixer_shader->SetShaderResourceView("rcache", nullptr);
	mixer_shader->SetShaderResourceView("rcache_value", nullptr);
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
	int32_t  groupsX = (int32_t)(ceil((float)post_process_pipeline->GetW() / 8.0f));
	int32_t  groupsY = (int32_t)(ceil((float)post_process_pipeline->GetH() / 8.0f));
	motion_blur->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
	motion_blur->SetMatrix4x4("prev_view_proj", prev_view_projection);
	motion_blur->SetShaderResourceView("input", motion_blur_map.SRV());
	motion_blur->SetInt("enabled", motion_blur_enabled && !IsDebugBufferActive());
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

		int32_t  groupsX = (int32_t)(ceil((float)dust_map.Width() / (8.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)dust_map.Height() / (8.0f)));

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

//Depth of field autofocus: measures the focal distance on the GPU as the scene
//depth at the center of the view. depth_map already stores the world distance from
//the camera to each visible surface (DepthPS writes length(worldPos - cameraPosition)),
//so the center texel is the distance to whatever the camera is aimed at - no
//projection maths, no raycast and, most importantly, no CPU readback: the result is
//left in a 1x1 texture that the DOF pass samples later in the same frame.
//
//Runs right after DrawDepth, the only point where depth_map is complete and not
//simultaneously bound as a UAV by the dust / lens flare passes.
void RenderSystem::ProcessAutoFocus() {
	if (!dof_autofocus || autofocus_shader == nullptr || autofocus_map.UAV() == nullptr) {
		return;
	}
	//Fraction of the way to the new measurement per frame. Low enough that a
	//camera sweeping past a near object racks focus smoothly instead of snapping,
	//high enough to settle within a few frames once the camera stops.
	autofocus_shader->SetFloat("smoothFactor", 0.15f);
	//Only used until something has been in view at all.
	autofocus_shader->SetFloat("defaultFocus", 20.0f);
	//9x9 depth texels around the center.
	autofocus_shader->SetInt("radius", 4);
	autofocus_shader->SetInt("reset", autofocus_reset);
	autofocus_shader->SetShaderResourceView("depthTexture", depth_map.SRV());
	autofocus_shader->SetUnorderedAccessView("focusOutput", autofocus_map.UAV());
	autofocus_shader->CopyAllBufferData();
	autofocus_shader->SetShader();
	dxcore->context->Dispatch(1, 1, 1);
	autofocus_shader->SetShaderResourceView("depthTexture", nullptr);
	autofocus_shader->SetUnorderedAccessView("focusOutput", nullptr);
	autofocus_shader->CopyAllBufferData();
	autofocus_reset = false;
}

void RenderSystem::ProcessLensFlare() {
	if (lens_flare_enabled && lens_flare_map.UAV() != nullptr) {
		lens_flare_map.Clear(zero);
		
		int32_t  groupsX = (int32_t)(ceil((float)lens_flare_map.Width() / (8.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)lens_flare_map.Height() / (8.0f)));
		
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
		lens_flare->SetInt("autofocusActive", dof_autofocus);
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
		lens_flare->SetShaderResourceView("autofocusTexture", autofocus_map.SRV());
		lens_flare->CopyAllBufferData();
		lens_flare->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		lens_flare->SetUnorderedAccessView("output", nullptr);
		lens_flare->SetUnorderedAccessView("depthTextureUAV", nullptr);
		lens_flare->SetShaderResourceView("rgbaNoise", nullptr);
		lens_flare->SetShaderResourceView("vol_data", nullptr);
		lens_flare->SetShaderResourceView("autofocusTexture", nullptr);
		UnprepareLights(lens_flare);
	}
}

void RenderSystem::CopyTexture(const Core::RenderTexture2D& input, Core::RenderTexture2D& output)
{
	int32_t  groupsX = (int32_t)(ceil((float)output.Width() / 8.0f));
	int32_t  groupsY = (int32_t)(ceil((float)output.Height() / 8.0f));
	copy_texture->SetShaderResourceView("input", input.SRV());
	copy_texture->SetUnorderedAccessView("output", output.UAV());
	copy_texture->CopyAllBufferData();
	copy_texture->SetShader();
	dxcore->context->Dispatch(groupsX, groupsY, 1);
	copy_texture->SetShaderResourceView("input", nullptr);
	copy_texture->SetUnorderedAccessView("output", nullptr);
	copy_texture->CopyAllBufferData();
}

void RenderSystem::ProcessMotion() {
	if (!cameras.GetData().empty()) {
		CameraEntity& cam_entity = cameras.GetData()[0];
		int32_t  groupsX = (int32_t)(ceil((float)motion_texture.Width() / 8.0f));
		int32_t  groupsY = (int32_t)(ceil((float)motion_texture.Height() / 8.0f));
		motion_shader->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
		motion_shader->SetMatrix4x4("prev_view_proj", prev_view_projection);
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

const Core::MeshData* RenderSystem::LodGeometry(Core::MeshData* data, int lod) {
	if (data == nullptr) {
		return nullptr;
	}
	//"No chain" and "level 0" are the same geometry, which is why nothing here
	//needs to know whether the mesh declares levels at all.
	if (lod <= 0 || lod >= (int)data->lods.size() || data->lods[lod].mesh == nullptr) {
		return data;
	}
	return data->lods[lod].mesh;
}

void RenderSystem::PrepareRT() {
	if (rt_quality != eRtQuality::OFF && rt_enabled && bvh_buffer != nullptr) {
		auto& device = dxcore->device;

		CameraEntity& cam_entity = cameras.GetData()[0];

		struct Node {
			ObjectInfo obj;
			MaterialProps mat;
			ID3D11ShaderResourceView* diff;
			//Only for the readout below; nothing traced reads them.
			uint32_t full_indices;
			uint32_t traced_indices;
		};
		std::map<float, Node> distance_map;

		bool enabled_layer[RT_NTEXTURES]{ false };
		enabled_layer[2] = rt_enabled & RT_INDIRECT_ENABLE;

		auto fill = [op = &enabled_layer[0], tr = &enabled_layer[1],
			ca = &cam_entity](const RenderTree& tree, std::map<float, Node>& distance_map) {
			for (const auto& shaders : tree) {
				for (const auto& mat : shaders.second) {
					for (const auto& de : mat.second.second.GetConstData()) {
						if (de.base->visible &&  de.mesh->GetData()->skeletons.empty()) {

							// Get the center and extents of the oriented box
							const float3& orientedBoxCenter = de.bounds->final_box.Center;
							const float3& orientedBoxExtents = de.bounds->final_box.Extents;

							//float distance = LENGHT_F3(de.transform->position - (ADD_F3_F3(ca->camera->world_position, ca->camera->direction)));
							float distance = LENGHT_F3(MAX_F3_F3(SUB_F3_F3(orientedBoxCenter - ca->transform->position, orientedBoxExtents), { 0.0f, 0.0f, 0.0f }));


							MaterialProps mo = de.mat->data->props;
							ID3D11ShaderResourceView* diffuse_text = de.mat->data->diffuse;

							ObjectInfo o;

							// Calculate the minimum and maximum points of the AABB
							o.aabb_min = { orientedBoxCenter.x - orientedBoxExtents.x,
											orientedBoxCenter.y - orientedBoxExtents.y,
											orientedBoxCenter.z - orientedBoxExtents.z };

							o.aabb_max = { orientedBoxCenter.x + orientedBoxExtents.x,
											orientedBoxCenter.y + orientedBoxExtents.y,
											orientedBoxCenter.z + orientedBoxExtents.z };

							Core::MeshData* data = de.mesh->GetData();
							//Every ray traces the coarsest level in the chain, whatever is
							//being drawn and whatever the quality setting says. Neither
							//consumer wants the detail: a reflection is a filtered,
							//denoised, half-resolution image of the scene and indirect
							//light is a diffuse integral accumulated over many frames, and
							//this pass is ~64% of a ray traced frame (see the cost table in
							//CLAUDE.md), so a mesh's silhouette in a reflection is the
							//cheapest thing in the frame to be approximate about.
							//
							//`lods` is finest-first with [0] being the mesh itself, so the
							//last entry is the answer and an empty chain means there is
							//only the mesh.
							const int trace_lod = data->lods.empty() ? 0 : (int)data->lods.size() - 1;
							const Core::MeshData* geometry = LodGeometry(data, trace_lod);

							o.position = orientedBoxCenter;
							o.world = de.transform->world_matrix;
							o.inv_world = de.transform->world_inv_matrix;

							o.density = de.mat->data->props.density;
							o.opacity = de.mat->data->props.opacity;

							o.vertex_offset = (uint32_t)geometry->vertexOffset;
							o.index_offset = (uint32_t)geometry->indexOffset;
							o.object_offset = (uint32_t)geometry->bvhOffset;

							if (o.opacity > 0.0f) {
								*op = true;
							}
							if (o.opacity < 1.0f) {
								*tr = true;
							}
							while (true) {
								if (!distance_map.contains(distance)) {
									distance_map[distance] = { o, mo, diffuse_text,
										data->indexCount, geometry->indexCount };
									break;
								}
								else {
									distance += 1.0f;
								}
							}
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

		fill(render_tree, distance_map);
		fill(render_pass2_tree, distance_map);
		sort(distance_map, objects, objectMaterials, diffuseTextures, nobjects);
		//An empty object list has no hierarchy to build, and Subdivide does not
		//survive being asked for one: the root gets a count of zero, neither
		//termination case matches, and it recurses into two equally empty children
		//until the stack runs out. Only reachable with nothing traceable on screen
		//(every drawable skinned, or hidden), which is why it went unnoticed while
		//the shaders ignored the result.
		if (nobjects <= 0) {
			rt_geometry_stats = RtGeometryStats{};
			return;
		}
		//Over the objects that were actually sent, which is the nearest MAX_OBJECTS of
		//them and not everything the trees hold.
		{
			RtGeometryStats stats;
			int index = 0;
			for (const auto& o : distance_map) {
				if (index++ >= nobjects) {
					break;
				}
				stats.objects++;
				stats.full_indices += o.second.full_indices;
				stats.traced_indices += o.second.traced_indices;
			}
			rt_geometry_stats = stats;
		}
		//From the AABBs, which are the entity bounds and so identical in both arrays.
		tbvh.Load(objects, nobjects);
	}	
}

void RenderSystem::ProcessGI() {
	if (rt_enabled & RT_INDIRECT_ENABLE && rt_quality != eRtQuality::OFF && rt_enabled && bvh_buffer != nullptr && nobjects > 0) {

		restir_pdf_curr = &restir_pdf[0];
		restir_pdf_prev = &restir_pdf[1];

		rt_texture_gi_curr = &rt_textures_gi[current];
		rt_texture_gi_prev = &rt_textures_gi[prev];
		rt_texture_gi_tmp[0] = &rt_textures_gi[2];
		rt_texture_gi_tmp[1] = &rt_textures_gi[3];
		rt_texture_gi_trace = &rt_textures_gi[4];
		rt_textures_gi_tiles.Clear(zero);

		std::lock_guard<std::mutex> lock(rt_mutex);
		CameraEntity& cam_entity = cameras.GetData()[0];

		//The top-level BVH this pass walks (USE_OBH) has to be the one PrepareRT
		//just built. It used to be uploaded only by ProcessRT, which does not run
		//when reflections and refractions are both off - so indirect light would
		//have traversed a hierarchy describing the objects of some earlier frame,
		//and on the first frame one that was never written at all. Uploading it
		//twice in a frame costs a copy of at most MAX_OBJECTS*2-1 nodes (6 KB).
		tbvh_buffer.Refresh(tbvh.Root(), 0, tbvh.Size());

		gi_shader->SetInt("kernel_size", RESTIR_KERNEL);
		gi_shader->SetInt("ray_count", RESTIR_PIXEL_RAYS);

		gi_shader->SetInt("nobjects", nobjects);
		gi_shader->SetInt("enabled", rt_enabled & (rt_quality != eRtQuality::OFF ? 0xFF : 0x00));
		gi_shader->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
		gi_shader->SetMatrix4x4("prev_view_proj", prev_view_projection);
		gi_shader->SetInt("frame_count", frame_count);
		gi_shader->SetData("objectMaterials", objectMaterials, nobjects * sizeof(MaterialProps));
		gi_shader->SetData("objectInfos", objects, nobjects * sizeof(ObjectInfo));
		gi_shader->SetMatrix4x4(VIEW, cam_entity.camera->view);
		gi_shader->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		gi_shader->SetShaderResourceView("position_map", position_map.SRV());
		gi_shader->SetShaderResourceView("depth_map", depth_map.SRV());
		gi_shader->SetShaderResourceView("motion_texture", motion_texture.SRV());
		gi_shader->SetShaderResourceView("prev_position_map", prev_position_map.SRV());
		gi_shader->SetShaderResourceView("ray0", rt_ray_sources0.SRV());
		gi_shader->SetShaderResourceView("ray1", rt_ray_sources1.SRV());

		float3 dir;
		XMStoreFloat3(&dir, cam_entity.camera->xm_direction);
		gi_shader->SetFloat(TIME, time);
		gi_shader->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		gi_shader->SetFloat3("cameraDirection", dir);
		gi_shader->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		gi_shader->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		gi_shader->SetShaderResourceViewArray("DiffuseTextures[0]", diffuseTextures, nobjects);

		PrepareLights(gi_shader);

		gi_shader->CopyAllBufferData();
		gi_shader->SetShader();

		dxcore->context->CSSetShaderResources(2, 1, bvh_buffer->SRV());
		dxcore->context->CSSetShaderResources(3, 1, tbvh_buffer.SRV());
		dxcore->context->CSSetShaderResources(4, 1, vertex_buffer->VertexSRV());
		dxcore->context->CSSetShaderResources(5, 1, vertex_buffer->IndexSRV());

		gi_shader->SetShaderResourceView("restir_pdf_0", restir_pdf_prev->SRV());
		gi_shader->SetShaderResourceView("restir_w_0", restir_w.SRV());
		gi_shader->SetUnorderedAccessView("restir_pdf_1", restir_pdf_curr->UAV());

		gi_shader->SetUnorderedAccessView("output", rt_texture_gi_trace->UAV());
		gi_shader->SetUnorderedAccessView("tiles_output", rt_textures_gi_tiles.UAV());

		//The counters are per frame, so they are zeroed before the two passes that
		//write them rather than accumulated across the run.
		rcache_stats.Clear(0);
		gi_shader->SetUnorderedAccessView("rcache", rcache.UAV());
		gi_shader->SetUnorderedAccessView("rcache_value", rcache_value.UAV());
		gi_shader->SetUnorderedAccessView("rcache_stats", rcache_stats.UAV());

		int groupsX = (int32_t)(ceil((float)rt_texture_gi_curr->Width() / (8.0f)));
		int groupsY = (int32_t)(ceil((float)rt_texture_gi_curr->Height() / (8.0f)));
		dxcore->context->Dispatch((uint32_t)ceil((float)groupsX), (uint32_t)ceil((float)groupsY), 1);

		gi_shader->SetShaderResourceView("restir_pdf_0", nullptr);
		gi_shader->SetShaderResourceView("restir_w_0", nullptr);
		gi_shader->SetUnorderedAccessView("restir_pdf_1", nullptr);

		gi_shader->SetUnorderedAccessView("output", nullptr);
		gi_shader->SetUnorderedAccessView("tiles_output", nullptr);
		gi_shader->SetUnorderedAccessView("rcache", nullptr);
		gi_shader->SetUnorderedAccessView("rcache_value", nullptr);
		gi_shader->SetUnorderedAccessView("rcache_stats", nullptr);
		gi_shader->SetShaderResourceView("position_map", nullptr);
		gi_shader->SetShaderResourceView("motion_texture", nullptr);
		gi_shader->SetShaderResourceView("ray0", nullptr);
		gi_shader->SetShaderResourceView("ray1", nullptr);
		gi_shader->SetShaderResourceView("prev_position_map", nullptr);

		UnprepareLights(gi_shader);
		gi_shader->CopyAllBufferData();

		groupsX = (int32_t)(ceil((float)rt_texture_gi_curr->Width() / (8.0f)));
		groupsY = (int32_t)(ceil((float)rt_texture_gi_curr->Height() / (8.0f)));

		gi_weights->SetShader();
		gi_weights->SetInt("ray_count", RESTIR_PIXEL_RAYS);
		gi_weights->SetShaderResourceView("restir_pdf_0", restir_pdf_curr->SRV());
		gi_weights->SetUnorderedAccessView("restir_pdf_1", restir_pdf_prev->UAV());
		gi_weights->SetUnorderedAccessView("restir_w_1", restir_w.UAV());
		gi_weights->SetInt("frame_count", frame_count);
		gi_weights->CopyAllBufferData();

		dxcore->context->Dispatch(groupsX, groupsY, 1);

		gi_weights->SetShaderResourceView("restir_pdf_0", nullptr);
		gi_weights->SetUnorderedAccessView("restir_pdf_1", nullptr);
		gi_weights->SetUnorderedAccessView("restir_w_1", nullptr);
		gi_weights->CopyAllBufferData();

		//Resolve the world cache: this frame's deposits become the value every
		//lookup reads, and cells nobody has fed in RC_MAX_AGE frames are freed. It
		//has to run after the trace has deposited and before anything reads a
		//lookup, which for now means before the next frame's trace.
		rcache_resolve->SetShader();
		rcache_resolve->SetInt("frame_count", frame_count);
		rcache_resolve->SetInt("reset", rcache_reset ? 1 : 0);
		rcache_resolve->SetUnorderedAccessView("rcache", rcache.UAV());
		rcache_resolve->SetUnorderedAccessView("rcache_value", rcache_value.UAV());
		rcache_resolve->SetUnorderedAccessView("rcache_stats", rcache_stats.UAV());
		rcache_resolve->CopyAllBufferData();
		dxcore->context->Dispatch(RADIANCE_CACHE_ENTRIES / 64, 1, 1);
		rcache_resolve->SetUnorderedAccessView("rcache", nullptr);
		rcache_resolve->SetUnorderedAccessView("rcache_value", nullptr);
		rcache_resolve->SetUnorderedAccessView("rcache_stats", nullptr);
		rcache_resolve->CopyAllBufferData();
		rcache_reset = false;

		//Never blocks: the copy taken this frame is read several frames from now. A
		//failed map leaves the previous reading in place, which is the right answer
		//for a counter nothing is gated on.
		//
		//Only while something is reading them. This is a CopyResource and a driver
		//Map on the render thread, and running it unconditionally put that on every
		//frame of every build for a counter that is idle almost always - measurable
		//as jitter in the editor's tick rather than in the frame time.
		rcache_stats_cpu.entries = RADIANCE_CACHE_ENTRIES;
		if (rcache_stats_requested &&
			frame_count - rcache_stats_request_frame < RADIANCE_CACHE_STATS_KEEPALIVE) {
			uint32_t raw[RADIANCE_CACHE_STATS_BYTES / sizeof(uint32_t)] = {};
			if (rcache_stats.Readback(raw, sizeof(raw))) {
				rcache_stats_cpu.live = raw[0];
				rcache_stats_cpu.touched = raw[1];
				rcache_stats_cpu.evicted = raw[2];
				//Sampled one thread per 8x8 group - see the note in GIRayTraceCS.
				rcache_stats_cpu.deposits = raw[3] * 64;
				rcache_stats_cpu.dropped = raw[4] * 64;
				rcache_stats_cpu.hits = raw[5];
				rcache_stats_cpu.misses = raw[6];
				rcache_stats_cpu.entries = RADIANCE_CACHE_ENTRIES;
			}
		}

		gi_average->SetInt("debug", rt_debug);
		gi_average->SetMatrix4x4("prev_view_proj", prev_view_projection);
		gi_average->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		gi_average->SetShaderResourceView("positions", rt_ray_sources0.SRV());
		gi_average->SetShaderResourceView("normals", rt_ray_sources1.SRV());
		gi_average->SetShaderResourceView("prev_output", rt_texture_gi_prev->SRV());
		gi_average->SetShaderResourceView("motion_texture", motion_texture.SRV());
		gi_average->SetShaderResourceView("prev_position_map", prev_position_map.SRV());
		gi_average->SetShaderResourceView("tiles_output", rt_textures_gi_tiles.SRV());
		gi_average->SetInt("kernel_size", RESTIR_HALF_KERNEL);
		gi_average->SetInt("frame_count", frame_count);
		//Pass 3 reads the world cache to fill in where the screen-space history
		//cannot. Read-only here - the SRV, not the UAV the tracer deposits through.
		gi_average->SetShaderResourceView("rcache", rcache.SRV());
		gi_average->SetShaderResourceView("rcache_value", rcache_value.SRV());
#if 1
		//Pass 1
		gi_average->SetInt("type", 1);
		gi_average->SetShaderResourceView("input", rt_texture_gi_trace->SRV());
		gi_average->SetUnorderedAccessView("output", rt_texture_gi_tmp[0]->UAV());
		gi_average->CopyAllBufferData();
		gi_average->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		gi_average->SetShaderResourceView("input", nullptr);
		gi_average->SetUnorderedAccessView("output", nullptr);
		gi_average->CopyAllBufferData();
#if 1
		//Pass 2 
		gi_average->SetInt("type", 2);
		gi_average->SetShaderResourceView("orig_input", rt_texture_gi_trace->SRV());
		gi_average->SetShaderResourceView("input", rt_texture_gi_tmp[0]->SRV());
		gi_average->SetUnorderedAccessView("output", rt_texture_gi_tmp[1]->UAV());
		gi_average->CopyAllBufferData();
		gi_average->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		gi_average->SetShaderResourceView("input", nullptr);
		gi_average->SetUnorderedAccessView("output", nullptr);
		gi_average->CopyAllBufferData();

		//Pass 3
		gi_average->SetInt("type", 3);
		gi_average->SetShaderResourceView("input", rt_texture_gi_tmp[1]->SRV());
		gi_average->SetUnorderedAccessView("output", rt_texture_gi_curr->UAV());
		gi_average->CopyAllBufferData();
		gi_average->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
#else
		//Pass 2 
		gi_average->SetInt("type", 2);
		gi_average->SetShaderResourceView("orig_input", rt_texture_gi_trace->SRV());
		gi_average->SetShaderResourceView("input", rt_texture_gi_tmp[0]->SRV());
		gi_average->SetUnorderedAccessView("output", rt_texture_gi_curr->UAV());
		gi_average->CopyAllBufferData();
		gi_average->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		gi_average->SetShaderResourceView("input", nullptr);
		gi_average->SetUnorderedAccessView("output", nullptr);
		gi_average->CopyAllBufferData();
#endif
#else
		//Pass 3
		gi_average->SetInt("type", 3);
		gi_average->SetShaderResourceView("orig_input", rt_texture_gi_trace->SRV());
		gi_average->SetShaderResourceView("input", rt_texture_gi_tmp[1]->SRV());
		gi_average->SetUnorderedAccessView("output", rt_texture_gi_curr->UAV());
		gi_average->CopyAllBufferData();
		gi_average->SetShader();
		dxcore->context->Dispatch(groupsX, groupsY, 1);
#endif
		gi_average->SetShaderResourceView("input", nullptr);
		gi_average->SetUnorderedAccessView("output", nullptr);
		gi_average->SetShaderResourceView("tiles_output", nullptr);
		gi_average->SetShaderResourceView("orig_input", nullptr);
		gi_average->SetShaderResourceView("positions", nullptr);
		gi_average->SetShaderResourceView("normals", nullptr);
		gi_average->SetShaderResourceView("prev_output", nullptr);
		gi_average->SetShaderResourceView("motion_texture", nullptr);
		gi_average->SetShaderResourceView("prev_position_map", nullptr);
		gi_average->SetShaderResourceView("rcache", nullptr);
		gi_average->SetShaderResourceView("rcache_value", nullptr);
		gi_average->CopyAllBufferData();
	}
	else {
		//Nothing feeds or resolves the cache when the indirect pass does not run, so
		//the counters would otherwise freeze at whatever they held when it last did -
		//a readout that says the cache is busy while the pass that fills it is off.
		//`entries` is a property of the table rather than of the frame, so it stays.
		rcache_stats_cpu = RadianceCacheStats{};
		rcache_stats_cpu.entries = RADIANCE_CACHE_ENTRIES;
	}
}

void RenderSystem::ProcessRT() {

	if (rt_enabled & (RT_REFLEX_ENABLE | RT_REFRACT_ENABLE)) {

		rt_texture_di_curr = rt_textures_di[current];
		rt_texture_di_prev = rt_textures_di[prev];

		if (rt_quality != eRtQuality::OFF && rt_enabled && bvh_buffer != nullptr && nobjects > 0) {

			rt_textures_gi_tiles.Clear(zero);

			std::lock_guard<std::mutex> lock(rt_mutex);
			CameraEntity& cam_entity = cameras.GetData()[0];

			tbvh_buffer.Refresh(tbvh.Root(), 0, tbvh.Size());

			ID3D11RenderTargetView* nullRenderTargetViews[1] = { nullptr };
			dxcore->context->OMSetRenderTargets(1, nullRenderTargetViews, nullptr);

			rt_di_shader->SetInt("kernel_size", RESTIR_KERNEL);
			rt_di_shader->SetInt("ray_count", RESTIR_PIXEL_RAYS);

			rt_di_shader->SetInt("kernel_size", RESTIR_KERNEL);
			rt_di_shader->SetInt("nobjects", nobjects);
			rt_di_shader->SetInt("enabled", rt_enabled & (rt_quality != eRtQuality::OFF ? 0xFF : 0x00));
			rt_di_shader->SetMatrix4x4("view_proj", cam_entity.camera->view_projection);
			rt_di_shader->SetMatrix4x4("prev_view_proj", prev_view_projection);
			rt_di_shader->SetInt("frame_count", frame_count);
			rt_di_shader->SetData("objectMaterials", objectMaterials, nobjects * sizeof(MaterialProps));
			rt_di_shader->SetData("objectInfos", objects, nobjects * sizeof(ObjectInfo));

			rt_di_shader->SetUnorderedAccessView("output0", rt_texture_di_curr[RT_TEXTURE_REFLEX].UAV());
			rt_di_shader->SetUnorderedAccessView("output1", rt_texture_di_curr[RT_TEXTURE_REFRACT].UAV());
			rt_di_shader->SetUnorderedAccessView("tiles_output", rt_textures_gi_tiles.UAV());

			rt_di_shader->SetShaderResourceView("prev_position_map", prev_position_map.SRV());
			rt_di_shader->SetShaderResourceView("position_map", position_map.SRV());
			rt_di_shader->SetShaderResourceView("depth_map", depth_map.SRV());
			rt_di_shader->SetShaderResourceView("motion_texture", motion_texture.SRV());
			rt_di_shader->SetUnorderedAccessView("bloom", rt_texture_di_curr[RT_TEXTURE_EMISSION].UAV());
			rt_di_shader->SetShaderResourceView("ray0", rt_ray_sources0.SRV());
			rt_di_shader->SetShaderResourceView("ray1", rt_ray_sources1.SRV());
			rt_di_shader->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());

			float3 dir;
			XMStoreFloat3(&dir, cam_entity.camera->xm_direction);
			rt_di_shader->SetFloat(TIME, time);
			rt_di_shader->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
			rt_di_shader->SetFloat3("cameraDirection", dir);
			rt_di_shader->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
			rt_di_shader->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
			rt_di_shader->SetShaderResourceViewArray("DiffuseTextures[0]", diffuseTextures, nobjects);

			PrepareLights(rt_di_shader);

			rt_di_shader->CopyAllBufferData();

			dxcore->context->CSSetShaderResources(2, 1, bvh_buffer->SRV());
			dxcore->context->CSSetShaderResources(3, 1, tbvh_buffer.SRV());
			dxcore->context->CSSetShaderResources(4, 1, vertex_buffer->VertexSRV());
			dxcore->context->CSSetShaderResources(5, 1, vertex_buffer->IndexSRV());
			rt_di_shader->SetShader();
			int32_t  groupsX = (int32_t)(ceil((float)rt_texture_di_curr[RT_TEXTURE_REFLEX].Width() / (8.0f)));
			int32_t  groupsY = (int32_t)(ceil((float)rt_texture_di_curr[RT_TEXTURE_REFLEX].Height() / (8.0f)));
			dxcore->context->Dispatch(groupsX, groupsY, 2);

			rt_di_shader->SetUnorderedAccessView("output0", nullptr);
			rt_di_shader->SetUnorderedAccessView("output1", nullptr);
			rt_di_shader->SetShaderResourceView("prev_position_map", nullptr);
			rt_di_shader->SetShaderResourceView("position_map", nullptr);
			rt_di_shader->SetShaderResourceView("motion_texture", nullptr);
			rt_di_shader->SetShaderResourceView("ray0", nullptr);
			rt_di_shader->SetShaderResourceView("ray1", nullptr);
			rt_di_shader->SetUnorderedAccessView("bloom", nullptr);
			rt_di_shader->SetShaderResourceView("rgbaNoise", nullptr);
			rt_di_shader->SetUnorderedAccessView("tiles_output", nullptr);

			UnprepareLights(rt_di_shader);
			rt_di_shader->CopyAllBufferData();

			//Denoiser
			rt_di_denoiser->SetInt("kernel_size", RESTIR_KERNEL);
			rt_di_denoiser->SetInt("debug", rt_debug);
			rt_di_denoiser->SetShaderResourceView("positions", rt_ray_sources0.SRV());
			rt_di_denoiser->SetShaderResourceView("normals", rt_ray_sources1.SRV());
			rt_di_denoiser->SetShaderResourceView("motion_texture", motion_texture.SRV());
			rt_di_denoiser->SetShaderResourceView("prev_position_map", prev_position_map.SRV());
			rt_di_denoiser->SetMatrix4x4(VIEW, cam_entity.camera->view);
			rt_di_denoiser->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
			//The temporal reprojection needs the view-projection the history was rendered
			//with, not this frame's - see the comment on it in DenoiserCS.hlsl. GIAverageCS
			//has had this for the same reason; this pass simply never had it plumbed in.
			rt_di_denoiser->SetMatrix4x4("prev_view_proj", prev_view_projection);
			rt_di_denoiser->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
			rt_di_denoiser->SetShaderResourceView("tiles_output", rt_textures_gi_tiles.SRV());

			static constexpr int textures[] = { RT_TEXTURE_REFLEX, RT_TEXTURE_REFRACT };
			static constexpr int ntextures = sizeof(textures) / sizeof(int);
			for (int i = 0; i < ntextures; ++i) {
				int ntexture = textures[i];

				groupsX = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Width() / (8.0f)));
				groupsY = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Height() / (8.0f)));

				rt_di_denoiser->SetShaderResourceView("input", rt_texture_di_curr[ntexture].SRV());
				rt_di_denoiser->SetUnorderedAccessView("output", texture_tmp.UAV());
				rt_di_denoiser->SetShaderResourceView("prev_output", rt_texture_di_prev[ntexture].SRV());
				rt_di_denoiser->SetInt("type", 1);
				rt_di_denoiser->SetInt("light_type", i);
				rt_di_denoiser->CopyAllBufferData();
				rt_di_denoiser->SetShader();
				groupsX = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Width() / (8.0f)));
				groupsY = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Height() / (8.0f)));
				dxcore->context->Dispatch(groupsX, groupsY, 1);
				rt_di_denoiser->SetShaderResourceView("input", nullptr);
				rt_di_denoiser->SetUnorderedAccessView("output", nullptr);
				rt_di_denoiser->CopyAllBufferData();

				//Denoiser
				rt_di_denoiser->SetShaderResourceView("input", texture_tmp.SRV());
				rt_di_denoiser->SetUnorderedAccessView("output", rt_texture_di_curr[ntexture].UAV());
				rt_di_denoiser->SetInt("type", 2);
				rt_di_denoiser->CopyAllBufferData();
				dxcore->context->Dispatch(groupsX, groupsY, 1);
				rt_di_denoiser->SetShaderResourceView("input", nullptr);
				rt_di_denoiser->SetUnorderedAccessView("output", nullptr);
				rt_di_denoiser->SetShaderResourceView("prev_output", nullptr);
				rt_di_denoiser->CopyAllBufferData();
			}

			rt_di_denoiser->SetShaderResourceView("prev_output", nullptr);
			rt_di_denoiser->SetShaderResourceView("normals", nullptr);
			rt_di_denoiser->SetShaderResourceView("positions", nullptr);
			rt_di_denoiser->SetShaderResourceView("motion_texture", nullptr);
			rt_di_denoiser->SetShaderResourceView("prev_position_map", nullptr);
			rt_di_denoiser->SetShaderResourceView("tiles_output", nullptr);

			rt_di_denoiser->CopyAllBufferData();

#if 0
			//Apply antialias
			int ntexture = RT_TEXTURE_INDIRECT;
			groupsX = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Width() / (8.0f)));
			groupsY = (int32_t)(ceil((float)rt_texture_di_curr[ntexture].Height() / (8.0f)));

			aa_shader->SetShaderResourceView("depthTexture", depth_map.SRV());
			aa_shader->SetShaderResourceView("normalTexture", rt_ray_sources1.SRV());
			aa_shader->SetShaderResourceView("input", rt_texture_di_curr[ntexture].SRV());
			aa_shader->SetInt("enabled", true);
			aa_shader->SetInt("size", 4);
			aa_shader->SetUnorderedAccessView("output", texture_tmp.UAV());
			aa_shader->CopyAllBufferData();
			aa_shader->SetShader();
			dxcore->context->Dispatch(groupsX, groupsY, 1);
			aa_shader->SetUnorderedAccessView("output", nullptr);
			aa_shader->SetShaderResourceView("input", nullptr);
			aa_shader->CopyAllBufferData();
			aa_shader->SetShaderResourceView("input", texture_tmp.SRV());
			aa_shader->SetUnorderedAccessView("output", rt_texture_di_curr[ntexture].UAV());
			aa_shader->SetInt("size", 4);
			aa_shader->CopyAllBufferData();
			aa_shader->SetShader();
			dxcore->context->Dispatch(groupsX, groupsY, 1);
			aa_shader->SetUnorderedAccessView("output", nullptr);
			aa_shader->SetShaderResourceView("input", nullptr);
			aa_shader->SetShaderResourceView("depthTexture", nullptr);
			aa_shader->SetShaderResourceView("normalTexture", nullptr);
			aa_shader->CopyAllBufferData();
#endif
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

void RenderSystem::PrepareMultiMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs,
	                                    Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                    Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	Core::MultiMaterialData* mm = material->multi_material;
	if (mm == nullptr) {
		return;
	}
	const uint32_t count = mm->multi_texture_count;
	for (uint32_t i = 0; i < count; ++i) {
		//A layer whose source material is missing contributes nothing, but its slot
		//still has to be cleared: leaving the previous draw's texture bound there
		//would blend an unrelated surface in, and the layer's op bits say the map
		//does not exist so nothing would ever sample it back out.
		Core::MaterialData* source = mm->multi_texture_data[i];
		multitext_diff[i] = (source != nullptr) ? source->diffuse : nullptr;
		multitext_norm[i] = (source != nullptr) ? source->normal : nullptr;
		multitext_spec[i] = (source != nullptr) ? source->spec : nullptr;
		multitext_ao[i] = (source != nullptr) ? source->ao : nullptr;
		multitext_arm[i] = (source != nullptr) ? source->arm : nullptr;
		multitext_disp[i] = (source != nullptr) ? source->high : nullptr;
		multitext_mask[i] = mm->multi_texture_mask[i];
	}
	if (vs != nullptr) {
		vs->SetInt(TESS_TYPE, mm->tessellation_type);
		vs->SetFloat(TESS_FACTOR, mm->tessellation_factor);
	}

	if (ds != nullptr) {
		ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, count);
		if (count > 0) {
			ds->SetData("packed_multi_texture_values", mm->multi_texture_value.data(), count * sizeof(float));
			ds->SetData("packed_multi_texture_uv_scales", mm->multi_texture_uv_scales.data(), count * sizeof(float));
			ds->SetData("packed_multi_texture_operations", mm->multi_texture_operation.data(), count * sizeof(uint32_t));
			//The domain shader displaces along the layer heights, so it needs the same
			//slope/height rules the pixel shader uses - otherwise a snow layer masked
			//off a cliff would still push the cliff's geometry out.
			ds->SetData("multi_texture_slope", mm->multi_texture_slope.data(), count * sizeof(float4));
			ds->SetData("multi_texture_height", mm->multi_texture_height.data(), count * sizeof(float4));
			ds->SetData("multi_texture_mask_uv", mm->multi_texture_mask_uv.data(), count * sizeof(float4));
			ds->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), count);
			ds->SetShaderResourceViewArray("multi_maskTexture[0]", multitext_mask.data(), count);
			ds->SetFloat(DISPLACEMENT_SCALE, mm->displacement_scale);
		}
	}

	if (ps != nullptr) {
		ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, count);
		if (count > 0) {
			ps->SetFloat("multi_parallax_scale", mm->multi_parallax_scale);
			ps->SetData("packed_multi_texture_values", mm->multi_texture_value.data(), count * sizeof(float));
			ps->SetData("packed_multi_texture_uv_scales", mm->multi_texture_uv_scales.data(), count * sizeof(float));
			ps->SetData("packed_multi_texture_operations", mm->multi_texture_operation.data(), count * sizeof(uint32_t));
			ps->SetData("multi_texture_slope", mm->multi_texture_slope.data(), count * sizeof(float4));
			ps->SetData("multi_texture_height", mm->multi_texture_height.data(), count * sizeof(float4));
			ps->SetData("multi_texture_mask_uv", mm->multi_texture_mask_uv.data(), count * sizeof(float4));
			ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", multitext_diff.data(), count);
			ps->SetShaderResourceViewArray("multi_normalTexture[0]", multitext_norm.data(), count);
			ps->SetShaderResourceViewArray("multi_specularTexture[0]", multitext_spec.data(), count);
			ps->SetShaderResourceViewArray("multi_aoTexture[0]", multitext_ao.data(), count);
			ps->SetShaderResourceViewArray("multi_armTexture[0]", multitext_arm.data(), count);
			ps->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), count);
			ps->SetShaderResourceViewArray("multi_maskTexture[0]", multitext_mask.data(), count);
		}
	}
}

void RenderSystem::UnprepareMultiMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs,
	                                      Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                      Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	Core::MultiMaterialData* mm = material->multi_material;
	if (mm == nullptr || mm->multi_texture_count == 0) {
		return;
	}
	const uint32_t count = mm->multi_texture_count;
	static ID3D11ShaderResourceView* zero_text[MAX_MULTI_TEXTURE] = {};

	if (ps) {
		ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);

		ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_normalTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_specularTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_aoTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_armTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, count);
		ps->SetShaderResourceViewArray("multi_maskTexture[0]", zero_text, count);
	}
	if (ds) {
		ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);
		ds->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, count);
		ds->SetShaderResourceViewArray("multi_maskTexture[0]", zero_text, count);
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
	//Static casters live in their own map, refreshed only every STATIC_SHADOW_REFRESH_PERIOD
	//frames, so it carries its own view matrix (the light may have drifted since).
	if (!scene_lighting.dir_static_shadows.empty()) {
		s->SetData(DIR_STATIC_PERSPECTIVE_VALUES, scene_lighting.dir_static_shadows_perspectives.data(), (int)(sizeof(float4x4) * scene_lighting.dir_static_shadows_perspectives.size()));
		s->SetShaderResourceViewArray(DIR_STATIC_SHADOW_MAP_TEXTURE, scene_lighting.dir_static_shadows.data(), (int)(scene_lighting.dir_static_shadows.size()));
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
	//Must be released before the next CastShadows(static) binds it as a depth target.
	if (!scene_lighting.dir_static_shadows.empty()) {
		s->SetShaderResourceViewArray(DIR_STATIC_SHADOW_MAP_TEXTURE, no_data, MAX_LIGHTS);
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
	if (ds != nullptr) {
		ds->SetMatrix4x4(WORLD, world);
		ds->CopyAllBufferData();
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
	lighted->dir_shadows_perspectives.reserve(MAX_LIGHTS * MAX_SHADOW_CASCADES);
	lighted->dir_static_shadows_perspectives.reserve(MAX_LIGHTS);

	for (auto const& l: dir_lights.GetData()) {
		lighted->dir_lights.push_back(l.light->GetData());
		lighted->dir_shadows.push_back(l.light->DepthResource());
		lighted->dir_static_shadows.push_back(l.light->StaticDepthResource());
		//Always MAX_SHADOW_CASCADES matrices per light, live or not: the shaders index
		//this array as [light * MAX_SHADOW_CASCADES + cascade], so the stride has to be
		//constant even for a light using a single cascade. DirLight::cascade_count is
		//what tells the shader how many of each light's entries mean anything.
		const float4x4* cascades = l.light->GetViewMatrix();
		for (int c = 0; c < MAX_SHADOW_CASCADES; ++c) {
			lighted->dir_shadows_perspectives.push_back(cascades[c]);
		}
		//The static map is one map per light, not a cascade set, so one matrix.
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
	position_map.Clear(max_depth);
	prev_position_map.Clear(max_depth);
	for (int i = 0; i < 2; ++i)
	{
		light_map[i].Clear(color);
	}
	bloom_map.Clear(color);
	temp_map.Clear(color);
	first_pass_texture.Clear(color);
	texture_tmp.Clear(zero);

	rt_ray_sources0.Clear(zero);
	rt_ray_sources1.Clear(zero);

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
		post_process_pipeline = pipeline;
		PostProcess* last = pipeline;
		pipeline->SetShaderResourceView("volLightTexture", vol_light_map.SRV());
		pipeline->SetShaderResourceView("bloomTexture", bloom_map.SRV());
		pipeline->SetShaderResourceView("dustTexture", dust_render_map.SRV());
		pipeline->SetShaderResourceView("lensFlareTexture", lens_flare_map.SRV());

		while (last->GetNext() != nullptr) {
			BaseDOFProcess* tmp = dynamic_cast<BaseDOFProcess*>(last);
			if (tmp != nullptr) {
				dof_effect = tmp;
			}
			LensEffect* lens = dynamic_cast<LensEffect*>(last);
			if (lens != nullptr) {
				lens_effect = lens;
			}
			last = last->GetNext();
		}
		last->SetTarget(dxcore, dxcore);
	}
	else {
		first_pass_target = dxcore;
	}
}

void RenderSystem::Update() {
	++frame_count;
	current = frame_count % 2;
	prev = 1 - current;

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
		vol_shader->SetShaderResourceView("rgbaNoise", rgba_noise_texture.SRV());
		vol_shader->SetMatrix4x4("view_inverse", cam_entity.camera->inverse_view);
		vol_shader->SetMatrix4x4("projection_inverse", cam_entity.camera->inverse_projection);
		vol_shader->SetUnorderedAccessView("output", vol_light_map.UAV());
		vol_shader->SetUnorderedAccessView("vol_data", vol_data.UAV());
		vol_shader->CopyAllBufferData();
		vol_shader->SetShader();
		int32_t  groupsX = (int32_t)(ceil((float)vol_light_map.Width() / (8.0f)));
		int32_t  groupsY = (int32_t)(ceil((float)vol_light_map.Height() / (8.0f)));
		dxcore->context->Dispatch(groupsX, groupsY, 1);
		vol_shader->SetUnorderedAccessView("output", nullptr);
		vol_shader->SetUnorderedAccessView("vol_data", nullptr);
		vol_shader->SetShaderResourceView("rgbaNoise", nullptr);
		UnprepareVolumetricShader(vol_shader);

		//Smooth frame
		blur_shader->SetUnorderedAccessView("input", vol_light_map.UAV());
		blur_shader->SetUnorderedAccessView("output", vol_light_map2.UAV());
		blur_shader->SetShaderResourceView("vol_data", vol_data.SRV());
		groupsX = (int32_t)(ceil((float)vol_light_map.Width() / 8.0f));
		groupsY = (int32_t)(ceil((float)vol_light_map.Height() / 8.0f));
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
#if 1
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

	rv[0] = { temp_map.RenderTarget() };
	context->OMSetRenderTargets(1, rv, nullptr);
#endif
	ProcessDust();
	ProcessLensFlare();
}

void RenderSystem::Draw() {
	DXCore* dxcore = DXCore::Get();
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();
	
	if (!cameras.GetData().empty() && scene_enabled) {
		
		rt_mutex.lock();
		rt_prepare = true;
		rt_signal.notify_all();
		rt_mutex.unlock();

		CameraEntity& cam_entity = cameras.GetData()[0];
		//First frame: no previous frame to have moved from.
		if (!prev_view_projection_valid) {
			prev_view_projection = cam_entity.camera->view_projection;
			prev_view_projection_valid = true;
		}

		matrix view = XMMatrixTranspose(XMLoadFloat4x4(&cam_entity.camera->view));
		matrix projection = XMMatrixTranspose(XMLoadFloat4x4(&cam_entity.camera->projection));
		float3 camera_position = cam_entity.camera->world_position;

		CheckSceneVisibility(render_tree);
		//Before CastShadows and DrawDepth, so every pass of this frame draws the same
		//geometry - see SelectLods.
		SelectLods(*(cam_entity.camera));
		static int count = 0;
		//Only one every STATIC_SHADOW_REFRESH_PERIOD frames we refresh static shadows (directional light can change location due to sky component)
		//but this is fine to just make the overhead of casting shadow of static objects almost zero (cost reduced by /STATIC_SHADOW_REFRESH_PERIOD)
		//...plus immediately whenever the set of static casters or their positions change,
		//so an edit shows up now instead of up to a whole period later.
		//...plus whenever a light's static map no longer covers what its cascades do.
		//The map is fitted to the widest cascade, which follows the camera, so walking
		//far enough carries the view off the footprint - and every static object's
		//shadow with it - until the period next comes round, which at 1000 frames is
		//many seconds of a scene with no shadows on any of the scenery.
		bool static_stale = false;
		for (DirectionalLightEntity& l : directional_lights.GetData()) {
			if (l.light->CastShadow() && l.light->StaticShadowStale()) {
				static_stale = true;
				break;
			}
		}
		const uint64_t static_shadow_signature = StaticShadowSignature();
		if ((count++ % STATIC_SHADOW_REFRESH_PERIOD) == 0 || static_stale ||
			static_shadow_signature != last_static_shadow_signature) {
			last_static_shadow_signature = static_shadow_signature;
			CastShadows(w, h, camera_position, view, projection, true);
		}
		//Both effects are re-enabled from the stored flags every frame, which is what
		//lets a debug buffer view suppress them for as long as it is up without
		//disturbing what the user chose.
		const bool debug_buffer = IsDebugBufferActive();
		if (dof_effect) {
			dof_effect->SetEnabled(dof_enabled && !debug_buffer);
			dof_effect->SetAutofocus(dof_autofocus);
		}
		if (lens_effect) {
			lens_effect->SetEnabled(lens_enabled && !debug_buffer);
			lens_effect->SetAberration(lens_aberration);
			lens_effect->SetGrain(lens_grain);
			lens_effect->SetVignette(lens_vignette);
		}

		current_light_map = &light_map[0];
		prev_light_map = nullptr;

		CastShadows(w, h, camera_position, view, projection, false);
		DrawDepth(w, h, camera_position, view, projection);
		ProcessAutoFocus();
		DrawSky(w, h, camera_position, view, projection);
		DrawScene(w, h, camera_position, view, projection, nullptr, first_pass_target, render_tree);
		if (second_pass_target != nullptr && !render_pass2_tree.empty()) {
			CheckSceneVisibility(render_pass2_tree);
			current_light_map = &light_map[1];
			prev_light_map = &light_map[0];
			CopyTexture(*prev_light_map, *current_light_map);
			DrawScene(w, h, camera_position, view, projection, first_pass_texture.SRV(), second_pass_target, render_pass2_tree);
		}
		//After every DrawScene, so the clouds test against a complete depth buffer and
		//composite over the finished scene colour; before ProcessMotion and everything
		//after it, which build the frame out of the buffers this writes into.
		DrawSplats(w, h, camera_position, view, projection);
		ProcessMotion();
		ProcessRT();
		ProcessGI();
		PostProcessLight();
		ProcessMix();
		ProcessAntiAlias();
		ProcessMotionBlur();

		if (post_process_pipeline != nullptr) {
			post_process_pipeline->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
			post_process_pipeline->SetShaderResourceView(AUTOFOCUS_TEXTURE, autofocus_map.SRV());
			post_process_pipeline->SetView(*(cam_entity.camera));
		}
		LatchPreviousFrame(*(cam_entity.camera));
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

void RenderSystem::SetRayTracing(bool reflex_enabled, bool refract_enabled, bool indirect_enabled) {
	std::lock_guard l(mutex);
	rt_enabled = (reflex_enabled?RT_REFLEX_ENABLE:0) | (refract_enabled?RT_REFRACT_ENABLE:0) | (indirect_enabled ? RT_INDIRECT_ENABLE : 0);
	ResetRTBBuffers();
}

void RenderSystem::GetRayTracing(bool& reflex_enabled, bool& refract_enabled, bool& indirect_enabled) const {
	reflex_enabled = rt_enabled & RT_REFLEX_ENABLE;
	refract_enabled = rt_enabled & RT_REFRACT_ENABLE;
	indirect_enabled = rt_enabled & RT_INDIRECT_ENABLE;
}

void RenderSystem::SetRayTracingQuality(eRtQuality quality) {
	std::lock_guard l(mutex);
	if (rt_quality != quality) {
		rt_quality = quality;
		switch (rt_quality) {
		case eRtQuality::LOW: RT_TEXTURE_RESOLUTION_DIVIDER = 3; break;
		case eRtQuality::MID: RT_TEXTURE_RESOLUTION_DIVIDER = 2; break;
		case eRtQuality::HIGH: RT_TEXTURE_RESOLUTION_DIVIDER = 1; break;
		}
		if (rt_quality != eRtQuality::OFF) {
			LoadRTResources();
		}
	}
}

void RenderSystem::ResetRTBBuffers() {
	for (int i = 0; i < RT_NTEXTURES; ++i) {
		for (int x = 0; x < 2; ++x) {
			rt_textures_di[x][i].Clear(zero);
		}
	}

	for (int x = 0; x < RT_GI_NTEXTURES; ++x) {
		rt_textures_gi[x].Clear(zero);
	}

	rt_textures_gi_tiles.Clear(zero);

	static const float nrays[4] = { RESTIR_PIXEL_RAYS, RESTIR_PIXEL_RAYS, RESTIR_PIXEL_RAYS, RESTIR_PIXEL_RAYS };

	for (int i = 0; i < 2; ++i)
	{
		light_map[i].Clear(zero);
		restir_pdf[i].Clear(zero);
		restir_w.Clear(nrays);
	}

	//The world cache is accumulated state exactly like the textures above, so it is
	//dropped with them. Cleared by the next resolve rather than here: the buffer may
	//be bound to an in-flight dispatch, and the resolve pass already walks every
	//entry, so this costs nothing extra.
	rcache_reset = true;
}

RenderSystem::eRtQuality RenderSystem::GetRayTracingQuality() const {
	return rt_quality;
}

void RenderSystem::SetDustEnabled(bool enabled) {
	dust_enabled = enabled;
}

bool RenderSystem::GetDustEnabled() const {
	return dust_enabled;
}

void RenderSystem::SetDustEffectArea(int32_t num_particles, const float3& area, const float3& offset) {
	std::lock_guard l(mutex);
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

void RenderSystem::SetAA(bool enabled) {
	aa_enabled = enabled;
}

bool RenderSystem::GetAA() const {
	return aa_enabled;
}

void RenderSystem::SetMotionBlur(bool enabled) {
	motion_blur_enabled = enabled;
}

bool RenderSystem::GetMotionBlur() const {
	return motion_blur_enabled;
}

void RenderSystem::SetDOF(bool enabled) {
	dof_enabled = enabled;
}

bool RenderSystem::GetDOF() const {
	return dof_enabled;
}

void RenderSystem::SetDofAutofocus(bool enabled) {
	//Re-enabling after a spell of manual focus (or of no measurement at all) leaves
	//a stale distance in the texture; adopt the next measurement outright rather
	//than smoothing away from it.
	autofocus_reset = autofocus_reset || (enabled && !dof_autofocus);
	dof_autofocus = enabled;
}

bool RenderSystem::GetDofAutofocus() const {
	return dof_autofocus;
}

//The amounts are held here rather than only on the stage so they survive a chain
//that has not been installed yet (or has been swapped), and are pushed to it every
//frame from Update() alongside the DOF settings.
static float ClampLensAmount(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void RenderSystem::SetLensEffects(bool enabled) {
	lens_enabled = enabled;
}

bool RenderSystem::GetLensEffects() const {
	return lens_enabled;
}

void RenderSystem::SetLensAberration(float amount) {
	lens_aberration = ClampLensAmount(amount);
}

float RenderSystem::GetLensAberration() const {
	return lens_aberration;
}

void RenderSystem::SetLensGrain(float amount) {
	lens_grain = ClampLensAmount(amount);
}

float RenderSystem::GetLensGrain() const {
	return lens_grain;
}

void RenderSystem::SetLensVignette(float amount) {
	lens_vignette = ClampLensAmount(amount);
}

float RenderSystem::GetLensVignette() const {
	return lens_vignette;
}

void RenderSystem::SetRTDebug(uint32_t debug) {
	rt_debug = debug;
}

uint32_t RenderSystem::GetRTDebug() const {
	return rt_debug;
}

//Names must stay in eDebugBuffer order; the editor menu and the `render
//debug_buffer` automation key both index this table.
const char* RenderSystem::DebugBufferName(eDebugBuffer buffer) {
	static const char* names[(int)eDebugBuffer::COUNT] = {
		"off", "scene", "light", "bloom", "emission", "reflection", "refraction",
		"indirect", "volumetric", "dust", "lens_flare", "depth", "position", "normal",
		"motion", "gi_cache", "gi_cache_conf", "ray_sources", "ray_dispersion",
		"ray_reflex", "ray_density", "ray_opacity"
	};
	const int i = (int)buffer;
	return (i >= 0 && i < (int)eDebugBuffer::COUNT) ? names[i] : "off";
}

void RenderSystem::SetDebugBuffer(eDebugBuffer buffer) {
	rt_debug = (rt_debug & ~RT_DEBUG_BUFFER_MASK) |
		((uint32_t)buffer & RT_DEBUG_BUFFER_MASK);
}

RenderSystem::eDebugBuffer RenderSystem::GetDebugBuffer() const {
	return (eDebugBuffer)(rt_debug & RT_DEBUG_BUFFER_MASK);
}

void RenderSystem::SetDebugFlag(uint32_t flag, bool enabled) {
	if (enabled) { rt_debug |= flag; }
	else { rt_debug &= ~flag; }
}

bool RenderSystem::GetDebugFlag(uint32_t flag) const {
	return (rt_debug & flag) != 0;
}

bool RenderSystem::IsDebugBufferActive() const {
	return GetDebugBuffer() != eDebugBuffer::OFF;
}

void RenderSystem::SetDebugGain(float gain) {
	debug_gain = max(gain, 0.0f);
}

float RenderSystem::GetDebugGain() const {
	return debug_gain;
}

void RenderSystem::SetSceneEnabled(bool enabled) {
	scene_enabled = enabled;
}

bool RenderSystem::GetSceneEnabled() const {
	return scene_enabled;
}





