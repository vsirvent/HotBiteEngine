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

#include "Utils.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

#include <Core/Json.h>

#include "Material.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			std::mutex texture_mutex;
			std::unordered_map<std::string, ID3D11ShaderResourceView*> textures;
			std::unordered_map<ID3D11ShaderResourceView*, std::string> textures_names;

			void ReleaseTexture(ID3D11ShaderResourceView* srv) {
				std::lock_guard<std::mutex> l(texture_mutex);
				if (srv != nullptr && srv->Release() == 0) {
					auto it0 = textures_names.find(srv);
					auto it1 = textures.find(it0->second);
					textures.erase(it1);
					textures_names.erase(it0);
				}
			}

			ID3D11ShaderResourceView* LoadTexture(const std::string& filename)
			{
				std::lock_guard<std::mutex> l(texture_mutex);
				ID3D11ShaderResourceView* srv = nullptr;
				HRESULT ret = S_OK;
				if (!filename.empty()) {
					auto it = textures.find(filename);
					if (it == textures.end()) {
						try {
							std::wstring ws(filename.begin(), filename.end());
							if (filename.find(".dds") != std::string::npos || filename.find(".DDS") != std::string::npos) {
								ret = DirectX::CreateDDSTextureFromFile(DXCore::Get()->device, DXCore::Get()->context, ws.c_str(), nullptr, &srv);
								if (!SUCCEEDED(ret)) {
									throw std::exception("CreateDDSTextureFromFile failed");
								}
								printf("Loaded DDS texture %s\n", filename.c_str());
							}
							else {
								ret = DirectX::CreateWICTextureFromFile(DXCore::Get()->device, DXCore::Get()->context, ws.c_str(), nullptr, &srv);
								if (!SUCCEEDED(ret)) {
									throw std::exception("CreateWICTextureFromMemory failed");
								}
							}

						}
						catch (std::exception e) {
							printf("Error: %s, error %d loading texture %s\n", e.what(), ret, filename.c_str());
						}
						if (srv != nullptr) {
							textures[filename] = srv;
							textures_names[srv] = filename;
						}
					}
					else {
						srv = it->second;
						srv->AddRef();
					}
				}
				return srv;
			}

			void MaterialData::_MaterialData() {
				//Set default shaders, after loading the scene the shaders can be changed
				shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("MainRenderVS.cso");
				shaders.hs = ShaderFactory::Get()->GetShader<SimpleHullShader>("MainRenderHS.cso");
				shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>("MainRenderDS.cso");
				shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>("MainRenderGS.cso");
				shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("MainRenderPS.cso");

				shadow_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("ShadowVS.cso");
				shadow_shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>("ShadowMapCubeGS.cso");

				depth_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("DepthVS.cso");
				depth_shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("DepthPS.cso");
			}

			MaterialData::MaterialData() {
				_MaterialData();
			}

			MaterialData::MaterialData(const std::string& n) : name(n) {
				_MaterialData();
			}

			MaterialData::MaterialData(const MaterialData& other) {
				*this = other;
			}

			MaterialData& MaterialData::operator=(const MaterialData& other) {
				if (init) {
					Release();
				}
				this->props = other.props;
				this->texture_names = other.texture_names;
				this->shaders = other.shaders;
				this->shadow_shaders = other.shadow_shaders;
				this->depth_shaders = other.depth_shaders;

				this->tessellation_type = other.tessellation_type;
				this->tessellation_factor = other.tessellation_factor;
				this->displacement_scale = other.displacement_scale;
				this->bloom_scale = other.bloom_scale;

				this->init = other.init;

				if (init) {
					diffuse = LoadTexture(texture_names.diffuse_texname);
					high = LoadTexture(texture_names.high_textname);
					normal = LoadTexture(texture_names.normal_textname);
					spec = LoadTexture(texture_names.spec_textname);
					ao = LoadTexture(texture_names.ao_textname);
					arm = LoadTexture(texture_names.arm_textname);
					emission = LoadTexture(texture_names.emission_textname);
					opacity = LoadTexture(texture_names.opacity_textname);
				}
				return *this;
			}

			MaterialData::~MaterialData() {
				Release();
			}

			void MaterialData::SetTexture(ID3D11ShaderResourceView*& texture, std::string& current_texture, const std::string& root, const std::string& file) {
				std::string new_texure = root + std::string("\\") + file;
				if (!file.empty() && current_texture != new_texure) {
					current_texture = new_texure;
					if (texture) {
						ReleaseTexture(texture);
					}
					if (!file.empty()) {
						texture = LoadTexture(current_texture);
					}
					else {
						texture = nullptr;
						current_texture.clear();
					}
				}
				else {
					current_texture.clear();
				}
			}

			void MaterialData::Load(const std::string& root, const std::string& mat) {
				nlohmann::json j = nlohmann::json::parse(mat);
				name = j["name"];

				props.diffuseColor = parseColorStringF4(j.value("diffuse_color", "#FFFFFFFF"));
				props.bloom_scale = j.value("bloom_scale", 0.0f);
				props.parallax_scale = j.value("parallax_scale", 0.0f);
				props.parallax_steps = j.value("parallax_steps", 0.0f);
				props.parallax_angle_steps = j.value("parallax_angle_steps", 0.0f);
				props.parallax_shadow_scale = j.value("parallax_shadow_scale", 0.0f);
				props.specIntensity = j.value("specular", 0.5f);
				props.emission = j.value("emission", 0.0f);
				props.emission_color = parseColorStringF3(j.value("emission_color", "#00000000"));
				props.opacity = j.value("opacity", 1.0f);
				props.density = j.value("density", 1.0f);
				props.rt_reflex = j.value("rt_reflex", 0.2f);
				props.flags = 0;

				tessellation_type = j.value("tess_type", 0);
				tessellation_factor = j.value("tess_factor", 0.0f);
				displacement_scale = j.value("displacement_scale", 0.0f);
				if (j.value("raytrace", false)) {
					props.flags |= RAY_TRACING_ENABLED_FLAG;
				}
				if (j.value("alpha_enabled", false)) {
					props.flags |= ALPHA_ENABLED_FLAG;
				}
				if (j.value("blend_enabled", false)) {
					props.flags |= BLEND_ENABLED_FLAG;
				}
				if (j.value("parallax_shadows", false)) {
					props.flags |= PARALLAX_SHADOW_ENABLED_FLAG;
				}
				props.alphaColor = parseColorStringF3(j.value("alpha_color", "#00000000"));
				props.alphaThreshold = j.value("alpha_threshold", 0.4f);
				
				SetTexture(diffuse, texture_names.diffuse_texname, root, j.value("diffuse_textname", ""));
				SetTexture(high, texture_names.high_textname, root, j.value("high_textname", ""));
				SetTexture(normal, texture_names.normal_textname, root, j.value("normal_textname", ""));
				SetTexture(spec, texture_names.spec_textname, root, j.value("spec_textname", ""));
				SetTexture(ao, texture_names.ao_textname, root, j.value("ao_textname", ""));
				SetTexture(arm, texture_names.arm_textname, root, j.value("arm_textname", ""));
				SetTexture(emission, texture_names.emission_textname, root, j.value("emission_textname", ""));
				SetTexture(opacity, texture_names.opacity_textname, root, j.value("opacity_textname", ""));

				shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(j.value("draw_vs", "MainRenderVS.cso"));
				shaders.hs = ShaderFactory::Get()->GetShader<SimpleHullShader>(j.value("draw_hs", "MainRenderHS.cso"));
				shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>(j.value("draw_ds", "MainRenderDS.cso"));
				shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>(j.value("draw_gs", "MainRenderGS.cso"));
				shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>(j.value("draw_ps", "MainRenderPS.cso"));

				shadow_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(j.value("shadow_vs", "ShadowVS.cso"));
				shadow_shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>(j.value("shadow_gs", "ShadowMapCubeGS.cso"));

				depth_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(j.value("depth_vs", "DepthVS.cso"));
				depth_shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>(j.value("depth_ps", "DepthPS.cso"));

				UpdateFlags();
				init = true;
			}

			void MaterialData::UpdateFlags() {
				if (diffuse != nullptr) {
					props.flags |= DIFFUSSE_MAP_ENABLED_FLAG;
				}
				if (high != nullptr) {
					props.flags |= PARALLAX_MAP_ENABLED_FLAG;
				}
				if (normal != nullptr) {
					props.flags |= NORMAL_MAP_ENABLED_FLAG;
				}
				if (spec != nullptr) {
					props.flags |= SPEC_MAP_ENABLED_FLAG;
				}
				if (ao != nullptr) {
					props.flags |= AO_MAP_ENABLED_FLAG;
				}
				if (arm != nullptr) {
					props.flags |= ARM_MAP_ENABLED_FLAG;
				}
				if (emission != nullptr) {
					props.flags |= EMISSION_MAP_ENABLED_FLAG;
				}
				if (opacity != nullptr) {
					props.flags |= OPACITY_MAP_ENABLED_FLAG;
				}
			}

			bool MaterialData::Init() {
				bool ret = false;
				Release();
				init = true;
				diffuse = LoadTexture(texture_names.diffuse_texname);
				high = LoadTexture(texture_names.high_textname);
				normal = LoadTexture(texture_names.normal_textname);
				spec = LoadTexture(texture_names.spec_textname);
				ao = LoadTexture(texture_names.ao_textname);
				arm = LoadTexture(texture_names.arm_textname);
				emission = LoadTexture(texture_names.emission_textname);
				opacity = LoadTexture(texture_names.opacity_textname);
				UpdateFlags();
				return ret;
			}

			void MaterialData::Release() {
				init = false;
				if (diffuse) {
					ReleaseTexture(diffuse);
					diffuse = nullptr;
				}
				if (normal) {
					ReleaseTexture(normal);
					normal = nullptr;
				}
				if (high) {
					ReleaseTexture(high);
					high = nullptr;
				}
				if (spec) {
					ReleaseTexture(spec);
					spec = nullptr;
				}
				if (ao) {
					ReleaseTexture(ao);
					ao = nullptr;
				}
				if (arm) {
					ReleaseTexture(arm);
					arm = nullptr;
				}
				if (emission) {
					ReleaseTexture(emission);
					emission = nullptr;
				}
				if (opacity) {
					ReleaseTexture(opacity);
					opacity = nullptr;
				}
			}

		}
	}
}