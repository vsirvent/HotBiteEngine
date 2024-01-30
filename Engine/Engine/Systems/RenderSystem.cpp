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

const std::string RenderSystem::WORLD = "world";
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
		first_pass_target = dxcore;		
		depth_target = dxcore;
		int w = dxcore->GetWidth();
		int h = dxcore->GetHeight();
		
		if (FAILED(light_map.Init(w, h))) {
			throw std::exception("light_map.Init failed");
		}
		if (FAILED(temp_map.Init(w, h))) {
			throw std::exception("temp_map.Init failed");
		}
		if (FAILED(depth_map.Init(w / 2, h / 2, DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT))) {
			throw std::exception("depth_map.Init failed");
		}
		if (FAILED(depth_view.Init(w/2, h/2))) {
			throw std::exception("depth_view.Init failed");
		}
		if (FAILED(first_pass_texture.Init(w, h))) {
			throw std::exception("first_pass_texture.Init failed");
		}
		if (FAILED(rgba_noise_texture.Init(RGA_NOISE_W, RGBA_NOISE_H, DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, (const uint8_t*)rgba_noise_map, RGBA_NOISE_LEN))) {
			throw std::exception("rgba_noise_texture.Init failed");
		}
		if (FAILED(rt_texture.Init(w/4, h/4, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS))) {
			throw std::exception("rt_texture.Init failed");
		}

		rt_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("RayTraceCS.cso");
		if (rt_shader == nullptr) {
			throw std::exception("raytrace shader.Init failed");
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
	temp_map.Release();
	first_pass_texture.Release();
	rgba_noise_texture.Release();
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
					if (de.base->visible && de.base->scene_visible && de.base->draw_depth) {
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
							((!static_shadows && !de.base->is_static && de.base->scene_visible) || (static_shadows && de.base->is_static))) {
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

	ID3D11RenderTargetView* rv[2] = { target->RenderTarget(), light_target };
	context->OMSetRenderTargets(2, rv, depth_target_view);
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
				ps->SetInt("disable_vol", prev_pass_texture != nullptr);
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
				if (mat.second.first->multi_texture_count > 0) {
					PrepareMultiMaterial(mat.second.first, vs, hs, ds, gs, ps);
					ds->CopyAllBufferData();
				}
				for (auto& de : mat.second.second.GetData()) {
					PrepareEntity(de, vs, hs, ds, gs, ps);
					if (de.base->visible && de.base->scene_visible) {						
						if (!normal_mesh_map) {
							ps->SetInt(MESH_NORMAL_MAP_ENABLE, false);
							ps->CopyAllBufferData();
						}
 						Mesh* mesh = de.mesh;
						DXCore::Get()->context->DrawIndexed((UINT)mesh->index_count, (UINT)mesh->index_offset, (INT)mesh->vertex_offset);
						draw_count++;
					}
					UnprepareEntity(de, vs, hs, ds, gs, ps);
					total_count++;
				}
				if (mat.second.first->multi_texture_count > 0) {
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
		DrawParticles(w, h, camera_position, view, projection, particle_tree);
	}

	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);
	context->RSSetState(dxcore->drawing_rasterizer);

	//Print stats
	static int n = 0;
	if (n++ % 500 == 0) {
		printf("Rendered %d / %d objects\n", draw_count, total_count);
	}
}

void RenderSystem::ProcessRT() {
	if (bvh_buffer != nullptr) {
		auto& device = dxcore->device;
		ObjectInfo objects[MAX_OBJECTS];

		auto fill = [](RenderTree& tree, ObjectInfo objects[MAX_OBJECTS], int size) {
			int count = 0;
			for (auto& shaders : tree) {
				for (auto& mat : shaders.second) {
					for (auto& de : mat.second.second.GetData()) {
						if (de.base->visible) {
							ObjectInfo* o = &objects[count++];
							// Get the center and extents of the oriented box
							float3 orientedBoxCenter = de.bounds->final_box.Center;
							float3 orientedBoxExtents = de.bounds->final_box.Extents;

							// Calculate the minimum and maximum points of the AABB
							o->aabb_min = { orientedBoxCenter.x - orientedBoxExtents.x,
											orientedBoxCenter.y - orientedBoxExtents.y,
											orientedBoxCenter.z - orientedBoxExtents.z };

							o->aabb_max = { orientedBoxCenter.x + orientedBoxExtents.x,
											orientedBoxCenter.y + orientedBoxExtents.y,
											orientedBoxCenter.z + orientedBoxExtents.z };

							o->vertex_offset = (uint32_t)de.mesh->GetData()->vertexOffset;
							o->index_offset = (uint32_t)de.mesh->GetData()->indexOffset;
							o->object_offset = (uint32_t)de.mesh->GetData()->bvhOffset;
							o->world = de.transform->world_inv_matrix;
							if (count >= size) {
								return count;
							}
						}
					}
				}
			}
			return count;
			};
		int len = fill(render_tree, objects, MAX_OBJECTS);
		len += fill(render_pass2_tree, objects + len, MAX_OBJECTS - len);
		rt_shader->SetInt("nobjects", len);
		rt_shader->SetData("objectInfos", objects, len * sizeof(ObjectInfo));
		rt_shader->SetUnorderedAccessView("output", rt_texture.UAV());
		
		CameraEntity& cam_entity = cameras.GetData()[0];
		float3 dir;
		XMStoreFloat3(&dir, cam_entity.camera->xm_direction);
		rt_shader->SetFloat(TIME, time);
		rt_shader->SetMatrix4x4(VIEW, cam_entity.camera->view);
		rt_shader->SetMatrix4x4("invView", cam_entity.camera->inverse_view_projection);
		rt_shader->SetMatrix4x4(PROJECTION, cam_entity.camera->projection);
		rt_shader->SetFloat3(CAMERA_POSITION, cam_entity.camera->world_position);
		rt_shader->SetFloat3("cameraDirection", dir);
		rt_shader->SetSamplerState(PCF_SAMPLER, dxcore->shadow_sampler);
		rt_shader->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		rt_shader->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
		rt_shader->CopyAllBufferData();

		dxcore->context->CSSetShaderResources(2, 1, bvh_buffer->SRV());
		dxcore->context->CSSetShaderResources(3, 1, vertex_buffer->VertexSRV());
		dxcore->context->CSSetShaderResources(4, 1, vertex_buffer->IndexSRV());

		rt_shader->SetShader();
		
		dxcore->context->Dispatch(1, 1, 1);
		rt_shader->SetUnorderedAccessView("output", nullptr);

	}
}

bool RenderSystem::IsVisible(const float3& camera_pos, const DrawableEntity& drawable, const matrix& view_projection, int w, int h) const {
	bool ret = false;
	if (drawable.base->draw_method == DRAW_ALWAYS) {
		return true;
	}
	float3 points[8];
	float3& worldpos = drawable.bounds->final_box.Center;
	//float3 dir = { worldpos.x - camera_pos.x, worldpos.y - camera_pos.y, worldpos.z - camera_pos.z };
	//float dist2 = abs(DIST2(dir));
	//if (dist2 < DIST2(drawable.bounds->final_box.Extents) * 4) {
	//	return true;
	//}
	float box_dist2 = DIST2(drawable.bounds->final_box.Extents);
	drawable.bounds->final_box.GetCorners(points);
	for (int i = 0; i < 8; ++i) {
		float3 diff = { points[i].x - camera_pos.x, points[i].y - camera_pos.y, points[i].z - camera_pos.z };
		float dist2 = abs(DIST2(diff));
		if (dist2 < box_dist2) {
			ret = true;
			break;
		}
		float2 screen = WorldToScreen(points[i], view_projection, (float)w, (float)h);
		if ((screen.x > 0 && screen.x < w) && (screen.y > 0 && screen.y < h)) {
			ret = true;
			break;
		}
	}
	return ret;
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
		ps->SetShaderResourceView("rgbaNoise", nullptr);
	}
	if (material->props.flags & BLEND_ENABLED_FLAG) {
		DXCore::Get()->context->OMSetBlendState(dxcore->no_blend, NULL, ~0U);
	}
}

void RenderSystem::PrepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs,
	                                    Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                    Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (material->multi_texture_count > 0) {
		for (uint32_t i = 0; i < material->multi_texture_count; ++i) {
			if (material->multi_texture_data[i] != nullptr) {
				multitext_diff[i] = material->multi_texture_data[i]->diffuse;
				multitext_norm[i] = material->multi_texture_data[i]->normal;
				multitext_spec[i] = material->multi_texture_data[i]->spec;
				multitext_ao[i] = material->multi_texture_data[i]->ao;
				multitext_arm[i] = material->multi_texture_data[i]->arm;
				multitext_disp[i] = material->multi_texture_data[i]->high;
				multitext_mask[i] = material->multi_texture_mask[i];
			}
		}
	}
	if (vs != nullptr) {
		vs->SetInt(TESS_TYPE, material->tessellation_type);
		vs->SetFloat(TESS_FACTOR, material->tessellation_factor);
	}

	if (ds != nullptr) {
		ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, material->multi_texture_count);
		if (material->multi_texture_count > 0) {
			ds->SetData("packed_multi_texture_values", material->multi_texture_value.data(), material->multi_texture_count * sizeof(float));
			ds->SetData("packed_multi_texture_uv_scales", material->multi_texture_uv_scales.data(), material->multi_texture_count * sizeof(float));
			ds->SetData("packed_multi_texture_operations", material->multi_texture_operation.data(), material->multi_texture_count * sizeof(uint32_t));
			ds->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), material->multi_texture_count);
			ds->SetFloat(DISPLACEMENT_SCALE, material->displacement_scale);
		}
	}
	
	if (ps != nullptr) {
		ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, material->multi_texture_count);
		if (material->multi_texture_count > 0) {		
			ps->SetFloat("multi_parallax_scale", material->multi_parallax_scale);
			ps->SetData("packed_multi_texture_values", material->multi_texture_value.data(), material->multi_texture_count * sizeof(float));
			ps->SetData("packed_multi_texture_uv_scales", material->multi_texture_uv_scales.data(), material->multi_texture_count * sizeof(float));
			ps->SetData("packed_multi_texture_operations", material->multi_texture_operation.data(), material->multi_texture_count * sizeof(uint32_t));
			ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", multitext_diff.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_normalTexture[0]", multitext_norm.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_specularTexture[0]", multitext_spec.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_aoTexture[0]", multitext_ao.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_armTexture[0]", multitext_arm.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_highTexture[0]", multitext_disp.data(), material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_maskTexture[0]", multitext_mask.data(), material->multi_texture_count);
		}
	}
}

void RenderSystem::UnprepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs,
	                                      Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds,
	                                      Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (material->multi_texture_count > 0) {
		static ID3D11ShaderResourceView* zero_text[MAX_MULTI_TEXTURE] = {};

		if (ps) {
			ps->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);
		
			ps->SetShaderResourceViewArray("multi_diffuseTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_normalTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_specularTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_aoTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_armTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, material->multi_texture_count);
			ps->SetShaderResourceViewArray("multi_maskTexture[0]", zero_text, material->multi_texture_count);
		}
		if (ds) {
			ds->SetInt(SimpleShaderKeys::MULTI_TEXTURE_COUNT, 0);
			ds->SetShaderResourceViewArray("multi_highTexture[0]", zero_text, material->multi_texture_count);
		}
	}
}

void RenderSystem::PrepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (ps != nullptr) {
		if (!ambient_lights.GetData().empty()) {
			ps->SetData(AMBIENT_LIGHT, &ambient_lights.GetData()[0].light->GetData(), sizeof(AmbientLight::Data));
		}
		ps->SetInt(DIRLIGHT_COUNT, (int)scene_lighting.dir_lights.size());
		if (!scene_lighting.dir_lights.empty()) {
			ps->SetData(DIR_LIGHTS, scene_lighting.dir_lights.data(), (int)(sizeof(DirectionalLight::Data) * scene_lighting.dir_lights.size()));
		}
		ps->SetInt(POINT_LIGHT_COUNT, (int)scene_lighting.point_lights.size());
		if (!scene_lighting.point_lights.empty()) {
			ps->SetData(POINT_LIGHTS, scene_lighting.point_lights.data(), (int)(sizeof(PointLight::Data) * scene_lighting.point_lights.size()));
		}
		if (!scene_lighting.shadows_perspectives.empty()) {
			ps->SetData(LIGHT_PERSPECTIVE_VALUES, scene_lighting.shadows_perspectives.data(), (int)(sizeof(float2) * scene_lighting.shadows_perspectives.size()));
			ps->SetShaderResourceViewArray(POINT_SHADOW_MAP_TEXTURE, scene_lighting.shadows.data(), (int)(scene_lighting.shadows.size()));
		}
		if (!scene_lighting.dir_shadows.empty()) {
			ps->SetData(DIR_PERSPECTIVE_VALUES, scene_lighting.dir_shadows_perspectives.data(), (int)(sizeof(float4x4) * scene_lighting.dir_shadows_perspectives.size()));
			ps->SetShaderResourceViewArray(DIR_SHADOW_MAP_TEXTURE, scene_lighting.dir_shadows.data(), (int)(scene_lighting.dir_shadows.size()));
		}
		//if (!scene_lighting.dir_static_shadows.empty()) {
		//	ps->SetData(DIR_STATIC_PERSPECTIVE_VALUES, scene_lighting.dir_static_shadows_perspectives.data(), (int)(sizeof(float4x4) * scene_lighting.dir_static_shadows_perspectives.size()));
		//	ps->SetShaderResourceViewArray(DIR_STATIC_SHADOW_MAP_TEXTURE, scene_lighting.dir_static_shadows.data(), (int)(scene_lighting.dir_static_shadows.size()));
		//}
	}
}

void RenderSystem::UnprepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
	if (ps != nullptr) {
		ID3D11ShaderResourceView* no_data[MAX_LIGHTS] = {};
		if (!scene_lighting.shadows.empty()) {
			ps->SetShaderResourceViewArray(POINT_SHADOW_MAP_TEXTURE, no_data, MAX_LIGHTS);
		}
		if (!scene_lighting.dir_shadows.empty()) {
			ps->SetShaderResourceViewArray(DIR_SHADOW_MAP_TEXTURE, no_data, MAX_LIGHTS);
		}
		//if (!scene_lighting.dir_static_shadows.empty()) {
		//	ps->SetShaderResourceViewArray(DIR_STATIC_SHADOW_MAP_TEXTURE, no_data, min((int)scene_lighting.dir_static_shadows.size(), MAX_LIGHTS));
		//}
	}
}

void RenderSystem::PrepareEntity(DrawableEntity& entity, SimpleVertexShader* vs, SimpleHullShader* hs, SimpleDomainShader* ds, SimpleGeometryShader* gs, SimplePixelShader* ps) {
	Event e(this, entity.base->id, EVENT_ID_PREPARE_ENTITY);
	e.SetParam<ShaderKey>(EVENT_PARAM_SHADER, ShaderKey{ vs, hs, ds, gs, ps });
	coordinator->SendEvent(e);
	const float4x4& world = entity.transform->world_matrix;
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
	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	depth_map.Clear(max_depth);
	depth_view.Clear();
	light_map.Clear(color);
	temp_map.Clear(color);
	first_pass_texture.Clear(color);
	rt_texture.Clear(zero);
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
		depth_target = pipeline;
		post_process_pipeline = pipeline;
		PostProcess* last = pipeline;
		pipeline->SetShaderResourceView(LIGHT_TEXTURE, light_map.SRV());
		pipeline->SetShaderResourceView("rtTexture", rt_texture.SRV());
		while (last->GetNext() != nullptr) {
			last = last->GetNext();
		}
		last->SetTarget(dxcore, dxcore);
	}
	else {
		first_pass_target = dxcore;
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
	ID3D11DeviceContext* context = DXCore::Get()->context;
	//for (int i = 0; i < 3; ++i) {
		ID3D11RenderTargetView* rv[1] = { temp_map.RenderTarget() };
		context->OMSetRenderTargets(1, rv, nullptr);
		context->RSSetViewports(1, &dxcore->viewport);
		context->RSSetState(dxcore->drawing_rasterizer);
		SimpleVertexShader* vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso");
		SimplePixelShader* ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("PostBlur.cso");
		vs->SetShader();
		ps->SetShader();
		ps->SetInt(SCREEN_W, light_map.Width());
		ps->SetInt(SCREEN_H, light_map.Height());
		ps->SetSamplerState(BASIC_SAMPLER, dxcore->basic_sampler);
		ps->SetShaderResourceView("renderTexture", light_map.SRV());
		ps->SetInt("horizontal", 1);
		ps->CopyAllBufferData();
		ScreenDraw::Get()->Draw();
		ps->SetShaderResourceView("renderTexture", nullptr);
		ps->CopyAllBufferData();
		rv[0] = { light_map.RenderTarget() };
		context->OMSetRenderTargets(1, rv, nullptr);
		ps->SetInt("horizontal", 0);
		ps->SetShaderResourceView("renderTexture", temp_map.SRV());
		ps->CopyAllBufferData();
		ScreenDraw::Get()->Draw();
		ps->SetShaderResourceView("renderTexture", nullptr);
	//}
}

void RenderSystem::Draw() {
	DXCore* dxcore = DXCore::Get();
	int w = dxcore->GetWidth();
	int h = dxcore->GetHeight();

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
		ProcessRT();
		CastShadows(w, h, camera_position, view, projection, false);
		DrawDepth(w, h, camera_position, view, projection);
		DrawSky(w, h, camera_position, view, projection);
		DrawScene(w, h, camera_position, view, projection, nullptr, first_pass_target, render_tree);
		if (second_pass_target != nullptr) {
			CheckSceneVisibility(render_pass2_tree);
			DrawScene(w, h, camera_position, view, projection, first_pass_texture.SRV(), second_pass_target, render_pass2_tree);
		}
		PostProcessLight();
		if (post_process_pipeline != nullptr) {
			post_process_pipeline->SetShaderResourceView(DEPTH_TEXTURE, depth_map.SRV());
			post_process_pipeline->SetView(cam_entity.camera->view, cam_entity.camera->inverse_view, cam_entity.camera->inverse_projection);
		}
	}
	if (post_process_pipeline != nullptr) {
		DXCore::Get()->context->RSSetViewports(1, &dxcore->viewport);
		post_process_pipeline->Process();
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

