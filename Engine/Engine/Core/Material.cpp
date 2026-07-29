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

#include <algorithm>

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

			//--- MultiMaterialData ------------------------------------------------------

			namespace {
				//The four rule fields as the shader wants them. `enabled` rides in .w
				//rather than in a separate array because the alternative is a second
				//constant array whose only content is a boolean.
				float4 RangeVector(bool enabled, float min_value, float max_value, float fade) {
					return float4{ min_value, max_value, fade, enabled ? 1.0f : 0.0f };
				}
			}

			void MultiMaterialData::Rebuild(const std::string& root_path,
				const FlatMap<std::string, MaterialData>& materials) {
				multi_texture_count = (uint32_t)(std::min)(layers.size(), (size_t)MAX_MULTI_TEXTURE);
				multi_texture_data.assign(multi_texture_count, nullptr);
				multi_texture_operation.assign(multi_texture_count, 0);
				multi_texture_value.assign(multi_texture_count, 0.0f);
				multi_texture_uv_scales.assign(multi_texture_count, 1.0f);
				multi_texture_slope.assign(multi_texture_count, float4{});
				multi_texture_height.assign(multi_texture_count, float4{});
				multi_texture_mask_uv.assign(multi_texture_count, float4{ 1.0f, 1.0f, 0.0f, 0.0f });
				multi_texture_mask.resize(multi_texture_count, nullptr);
				resolved_masks.resize(multi_texture_count);

				for (uint32_t i = 0; i < multi_texture_count; ++i) {
					const MultiMaterialLayer& layer = layers[i];
					//The op field carries both the authored blend mode and, below, the
					//derived "this map exists" bits, so start from the authored half only.
					uint32_t op = layer.op & TEXT_OP_MASK;
					op |= (uint32_t)(layer.mask_channel & 3) << TEXT_MASK_CHANNEL_SHIFT;
					if (layer.mask_invert) { op |= TEXT_MASK_INV; }
					if (layer.mask_noise) { op |= TEXT_MASK_NOISE; }
					if (layer.uv_noise) { op |= TEXT_UV_NOISE; }
					if (layer.slope_enabled) { op |= TEXT_SLOPE; }
					if (layer.height_enabled) { op |= TEXT_HEIGHT; }

					const std::string mask_path = layer.mask.empty()
						? std::string() : root_path + "\\" + layer.mask;
					if (resolved_masks[i] != mask_path) {
						resolved_masks[i] = mask_path;
						multi_texture_mask[i] = mask_path.empty() ? nullptr : LoadTexture(mask_path);
					}
					ID3D11ShaderResourceView* live = GetLiveMask(i);
					if (live != nullptr) {
						multi_texture_mask[i] = live;
					}
					if (multi_texture_mask[i] != nullptr) {
						op |= TEXT_MASK;
					}

					MaterialData* source = materials.Get(layer.material);
					multi_texture_data[i] = source;
					if (source != nullptr) {
						//Which maps the source material actually has. A layer asking for a
						//map its material does not carry must not sample the array slot,
						//which holds whatever the previous draw left there.
						if (source->diffuse != nullptr) { op |= TEXT_DIFF; }
						if (source->normal != nullptr) { op |= TEXT_NORM; }
						if (source->spec != nullptr) { op |= TEXT_SPEC; }
						if (source->ao != nullptr) { op |= TEXT_AO; }
						if (source->arm != nullptr) { op |= TEXT_ARM; }
						if (source->high != nullptr) { op |= TEXT_DISP; }
					}

					multi_texture_operation[i] = op;
					multi_texture_value[i] = layer.value;
					multi_texture_uv_scales[i] = layer.uv_scale;
					multi_texture_slope[i] = RangeVector(layer.slope_enabled, layer.slope_min,
						layer.slope_max, layer.slope_fade);
					multi_texture_height[i] = RangeVector(layer.height_enabled, layer.height_min,
						layer.height_max, layer.height_fade);
					multi_texture_mask_uv[i] = float4{ layer.mask_uv_scale, layer.mask_uv_scale,
						layer.mask_uv_offset.x, layer.mask_uv_offset.y };
				}
			}

			void MultiMaterialData::SetLiveMask(uint32_t layer, ID3D11ShaderResourceView* srv) {
				if (live_masks.size() <= layer) {
					live_masks.resize(layer + 1, nullptr);
				}
				//Whether multi_texture_mask[layer]'s *current* entry is a reference this
				//object owns (a LoadTexture() call in Rebuild or in the reload branch
				//below) versus a caller-owned live texture (MaskPaint's session SRV,
				//released by MaskPaint::ReleaseGpu, never by this object) - decided by
				//whether a live override was active a moment ago, before it changes below.
				const bool owned_current_mask = (layer < live_masks.size()) && (live_masks[layer] == nullptr);
				live_masks[layer] = srv;
				//Take effect now rather than at the next unrelated Rebuild: the caller is a
				//brush, and a stroke that only appears when something else changes reads as
				//the tool not working.
				if (layer < multi_texture_mask.size()) {
					if (owned_current_mask && multi_texture_mask[layer] != nullptr) {
						ReleaseTexture(multi_texture_mask[layer]);
					}
					if (srv != nullptr) {
						multi_texture_mask[layer] = srv;
						multi_texture_operation[layer] |= TEXT_MASK;
					}
					else {
						multi_texture_mask[layer] = resolved_masks[layer].empty()
							? nullptr : LoadTexture(resolved_masks[layer]);
						if (multi_texture_mask[layer] == nullptr) {
							multi_texture_operation[layer] &= ~TEXT_MASK;
						}
					}
				}
			}

			ID3D11ShaderResourceView* MultiMaterialData::GetLiveMask(uint32_t layer) const {
				return (layer < live_masks.size()) ? live_masks[layer] : nullptr;
			}

			nlohmann::json MultiMaterialData::ToJson() const {
				nlohmann::json j;
				j["name"] = name;
				j["count"] = (uint32_t)layers.size();
				j["parallax_scale"] = multi_parallax_scale;
				j["tess_type"] = tessellation_type;
				j["tess_factor"] = tessellation_factor;
				j["displacement_scale"] = displacement_scale;

				nlohmann::json textures = nlohmann::json::array();
				for (size_t i = 0; i < layers.size(); ++i) {
					const MultiMaterialLayer& layer = layers[i];
					nlohmann::json t;
					t["layer"] = (int)i;
					t["texture"] = layer.material;
					t["mask"] = layer.mask;
					//The original format's shape: the blend mode alone, with the derived
					//map bits left out. Writing the whole runtime `op` would bake this
					//session's texture set into the file.
					t["op"] = layer.op & TEXT_OP_MASK;
					t["value"] = layer.value;
					t["uv_scale"] = layer.uv_scale;
					t["mask_noise"] = layer.mask_noise ? 1 : 0;
					t["uv_noise"] = layer.uv_noise ? 1 : 0;
					t["mask_channel"] = layer.mask_channel;
					t["mask_invert"] = layer.mask_invert ? 1 : 0;
					t["mask_uv_scale"] = layer.mask_uv_scale;
					t["mask_uv_offset"] = nlohmann::json{ {"u", layer.mask_uv_offset.x},
						{"v", layer.mask_uv_offset.y} };
					//Written even when disabled: every FromJson reads a missing key as
					//"leave alone", and the editor's undo replays an earlier ToJson - so a
					//key omitted because the rule was off could never be turned back off.
					t["slope"] = nlohmann::json{ {"enabled", layer.slope_enabled},
						{"min", layer.slope_min}, {"max", layer.slope_max}, {"fade", layer.slope_fade} };
					t["height"] = nlohmann::json{ {"enabled", layer.height_enabled},
						{"min", layer.height_min}, {"max", layer.height_max}, {"fade", layer.height_fade} };
					textures.push_back(t);
				}
				j["textures"] = textures;
				return j;
			}

			void MultiMaterialData::FromJson(const nlohmann::json& j, const std::string& root_path,
				const FlatMap<std::string, MaterialData>& materials) {
				name = j.value("name", name);
				multi_parallax_scale = j.value("parallax_scale", multi_parallax_scale);
				tessellation_type = j.value("tess_type", tessellation_type);
				tessellation_factor = j.value("tess_factor", tessellation_factor);
				displacement_scale = j.value("displacement_scale", displacement_scale);

				//"count" is what the original format declared; the array is authoritative
				//when the two disagree, since a short array with a large count used to
				//leave unwritten layers pointing at nothing.
				const nlohmann::json empty = nlohmann::json::array();
				const nlohmann::json& textures = j.contains("textures") ? j["textures"] : empty;
				size_t count = (size_t)j.value("count", (uint32_t)textures.size());
				count = (std::min)((std::max)(count, textures.size()), (size_t)MAX_MULTI_TEXTURE);
				layers.assign(count, MultiMaterialLayer{});

				for (const auto& t : textures) {
					const size_t index = (size_t)t.value("layer", -1);
					if (index >= layers.size()) {
						continue;
					}
					MultiMaterialLayer& layer = layers[index];
					layer.material = t.value("texture", std::string());
					layer.mask = t.value("mask", std::string());
					layer.op = t.value("op", (uint32_t)TEXT_OP_MIX) & TEXT_OP_MASK;
					layer.value = t.value("value", 1.0f);
					layer.uv_scale = t.value("uv_scale", 1.0f);
					layer.mask_noise = t.value("mask_noise", 0) != 0;
					layer.uv_noise = t.value("uv_noise", 0) != 0;
					layer.mask_channel = (std::max)(0, (std::min)(3, t.value("mask_channel", 0)));
					layer.mask_invert = t.value("mask_invert", 0) != 0;
					layer.mask_uv_scale = t.value("mask_uv_scale", 1.0f);
					if (t.contains("mask_uv_offset")) {
						const auto& o = t["mask_uv_offset"];
						layer.mask_uv_offset = { o.value("u", 0.0f), o.value("v", 0.0f) };
					}
					if (t.contains("slope")) {
						const auto& s = t["slope"];
						layer.slope_enabled = s.value("enabled", false);
						layer.slope_min = s.value("min", 0.0f);
						layer.slope_max = s.value("max", 1.0f);
						layer.slope_fade = s.value("fade", 0.1f);
					}
					if (t.contains("height")) {
						const auto& h = t["height"];
						layer.height_enabled = h.value("enabled", false);
						layer.height_min = h.value("min", 0.0f);
						layer.height_max = h.value("max", 0.0f);
						layer.height_fade = h.value("fade", 1.0f);
					}
				}
				Rebuild(root_path, materials);
			}

			bool MultiMaterialData::LoadMultitexture(const std::string& json_str,
				const std::string& root_path, const FlatMap<std::string, MaterialData>& materials) {
				try {
					FromJson(nlohmann::json::parse(json_str), root_path, materials);
				}
				catch (const std::exception& e) {
					LOG_WARN("MultiMaterialData::LoadMultitexture: %s", e.what());
					return false;
				}
				return true;
			}

			void MaterialData::_MaterialData() {
				//Logged once: the layout Engine.lib itself compiled MaterialData with,
				//to compare against what any other binary linking against it (a stale
				//Engine.lib, or headers that diverged) thinks the layout is. A mismatch
				//here is the class-layout hazard CLAUDE.md warns about; this is the
				//direct proof instead of inferring it from a misaligned-read disassembly.
				static bool logged_layout = false;
				if (!logged_layout) {
					logged_layout = true;
					LOG_INFO("Engine: sizeof(MaterialData)=%zu alignof(MaterialData)=%zu offsetof(props)=%zu",
						sizeof(MaterialData), alignof(MaterialData), offsetof(MaterialData, props));
				}
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
				//The name is part of the material's identity: it is how an entity
				//references it when a scene is saved and reloaded. Every existing caller
				//happened to call Load() right after copying, which reassigns the name,
				//so omitting it here went unnoticed - but a material built in code and
				//never Load()ed would end up nameless and impossible to resolve.
				this->name = other.name;
				this->source_json = other.source_json;
				this->props = other.props;
				this->texture_names = other.texture_names;
				this->shaders = other.shaders;
				this->shadow_shaders = other.shadow_shaders;
				this->depth_shaders = other.depth_shaders;
				this->shader_names = other.shader_names;
				//Both halves of the multi-material reference: the name is authoring data
				//that has to survive a copy (Save writes it), and the pointer is a borrowed
				//view of the world's registry, so copying it shares rather than duplicates.
				this->multi_material_name = other.multi_material_name;
				this->multi_material = other.multi_material;

				this->tessellation_type = other.tessellation_type;
				this->tessellation_factor = other.tessellation_factor;
				this->displacement_scale = other.displacement_scale;

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
				source_json = j;
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
				//Only the name: the stack itself lives in the world's registry, which is
				//filled from the same file's "multi_materials" array and may not have been
				//read yet. World::ResolveMultiMaterials binds the pointer afterwards.
				multi_material_name = j.value("multi_material", std::string());
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
				SetTexture(diffuse, texture_names.diffuse_texname, root, j.value("diffuse_textname", ""));
				SetTexture(high, texture_names.high_textname, root, j.value("high_textname", ""));
				SetTexture(normal, texture_names.normal_textname, root, j.value("normal_textname", ""));
				SetTexture(spec, texture_names.spec_textname, root, j.value("spec_textname", ""));
				SetTexture(ao, texture_names.ao_textname, root, j.value("ao_textname", ""));
				SetTexture(arm, texture_names.arm_textname, root, j.value("arm_textname", ""));
				SetTexture(emission, texture_names.emission_textname, root, j.value("emission_textname", ""));
				SetTexture(opacity, texture_names.opacity_textname, root, j.value("opacity_textname", ""));

				//Remember the names as well as the resolved pointers, so the material can
				//report and change its own shaders later (see MaterialShaderNames).
				shader_names.draw_vs = j.value("draw_vs", shader_names.draw_vs);
				shader_names.draw_hs = j.value("draw_hs", shader_names.draw_hs);
				shader_names.draw_ds = j.value("draw_ds", shader_names.draw_ds);
				shader_names.draw_gs = j.value("draw_gs", shader_names.draw_gs);
				shader_names.draw_ps = j.value("draw_ps", shader_names.draw_ps);
				shader_names.shadow_vs = j.value("shadow_vs", shader_names.shadow_vs);
				shader_names.shadow_gs = j.value("shadow_gs", shader_names.shadow_gs);
				shader_names.depth_vs = j.value("depth_vs", shader_names.depth_vs);
				shader_names.depth_ps = j.value("depth_ps", shader_names.depth_ps);

				shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(shader_names.draw_vs);
				shaders.hs = ShaderFactory::Get()->GetShader<SimpleHullShader>(shader_names.draw_hs);
				shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>(shader_names.draw_ds);
				shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>(shader_names.draw_gs);
				shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>(shader_names.draw_ps);

				shadow_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(shader_names.shadow_vs);
				shadow_shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>(shader_names.shadow_gs);

				depth_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(shader_names.depth_vs);
				depth_shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>(shader_names.depth_ps);

				UpdateFlags();
				init = true;
			}

			bool MaterialData::SetShaders(const MaterialShaderNames& names) {
				//Resolve everything into locals first. ShaderFactory::GetShader returns
				//null when a name does not load as the stage requested, and a material
				//with a null draw shader is undrawable - so nothing is adopted until the
				//whole set is known good.
				MaterialShaders draw;
				MaterialShaders shadow;
				MaterialShaders depth;
				ShaderFactory* factory = ShaderFactory::Get();

				draw.vs = factory->GetShader<SimpleVertexShader>(names.draw_vs);
				draw.hs = factory->GetShader<SimpleHullShader>(names.draw_hs);
				draw.ds = factory->GetShader<SimpleDomainShader>(names.draw_ds);
				draw.gs = factory->GetShader<SimpleGeometryShader>(names.draw_gs);
				draw.ps = factory->GetShader<SimplePixelShader>(names.draw_ps);
				shadow.vs = factory->GetShader<SimpleVertexShader>(names.shadow_vs);
				shadow.gs = factory->GetShader<SimpleGeometryShader>(names.shadow_gs);
				depth.vs = factory->GetShader<SimpleVertexShader>(names.depth_vs);
				depth.ps = factory->GetShader<SimplePixelShader>(names.depth_ps);

				if (draw.vs == nullptr || draw.hs == nullptr || draw.ds == nullptr ||
					draw.gs == nullptr || draw.ps == nullptr || shadow.vs == nullptr ||
					shadow.gs == nullptr || depth.vs == nullptr || depth.ps == nullptr) {
					printf("MaterialData::SetShaders: %s - one or more shaders failed to "
						"load as the requested stage; material left unchanged\n", name.c_str());
					return false;
				}

				shaders = draw;
				shadow_shaders = shadow;
				depth_shaders = depth;
				shader_names = names;
				return true;
			}

			nlohmann::json MaterialData::Save(const std::string& root) const {
				//Start from whatever this material was loaded with, so keys Load() ignores
				//survive the round trip untouched, then overwrite everything Load() reads.
				nlohmann::json j = source_json.is_object() ? source_json : nlohmann::json::object();

				//...except the properties the engine no longer has. These were never read
				//by Load (so they have had no effect on how anything renders for a long
				//time) and nothing writes them any more; carrying them forward would keep
				//advertising material fields that do not exist. "alhpa_*" are the
				//misspelled twins of "alpha_*" found in hand-authored levels - dead for the
				//same reason, and worth removing so a typo does not read as a real setting.
				for (const char* retired : { "ambient_color", "alpha_color", "alhpa_color",
					"alhpa_enabled" }) {
					j.erase(retired);
				}

				//SetTexture built these as root + "\" + file; undo exactly that. A path
				//that does not sit under root is written as-is - wrong is better than
				//silently repointing the material at a file that happens to share a name.
				const std::string prefix = root + std::string("\\");
				auto relative = [&prefix](const std::string& full) -> std::string {
					if (full.empty()) {
						return std::string();
					}
					if (full.size() > prefix.size() && full.compare(0, prefix.size(), prefix) == 0) {
						return full.substr(prefix.size());
					}
					return full;
				};

				j["name"] = name;
				j["diffuse_color"] = colorStringFromF4(props.diffuseColor);
				j["bloom_scale"] = props.bloom_scale;
				j["parallax_scale"] = props.parallax_scale;
				j["parallax_steps"] = props.parallax_steps;
				j["parallax_angle_steps"] = props.parallax_angle_steps;
				j["parallax_shadow_scale"] = props.parallax_shadow_scale;
				j["specular"] = props.specIntensity;
				j["emission"] = props.emission;
				//emission_color is an RGB value, but the files carry it in the 8-digit
				//"#RRGGBBAA" form and parseColorStringF3 throws the alpha away. Writing a
				//synthesized alpha would rewrite this key for every material in the file
				//on the first save, so keep whatever alpha came in (and match how these
				//files are authored - "00" - for a material that had none).
				{
					const std::string previous = j.value("emission_color", "");
					const std::string alpha = (previous.size() == 9) ? previous.substr(7, 2) : "00";
					std::string emission_rgb = colorStringFromF3(props.emission_color);
					j["emission_color"] = emission_rgb.substr(0, 7) + alpha;
				}
				j["opacity"] = props.opacity;
				j["density"] = props.density;
				j["rt_reflex"] = props.rt_reflex;

				j["tess_type"] = tessellation_type;
				j["tess_factor"] = tessellation_factor;
				j["displacement_scale"] = displacement_scale;
				//Written unconditionally, empty included: a material that *stopped* being
				//a multi-material has to say so, or reloading the file would reattach the
				//stack the key still named.
				j["multi_material"] = multi_material_name;

				//Only the four flags Load() sets from the file are written back; the rest
				//of props.flags is derived from which texture maps are present and is
				//recomputed by UpdateFlags() on load.
				j["raytrace"] = (props.flags & RAY_TRACING_ENABLED_FLAG) != 0;
				j["alpha_enabled"] = (props.flags & ALPHA_ENABLED_FLAG) != 0;
				j["blend_enabled"] = (props.flags & BLEND_ENABLED_FLAG) != 0;
				j["parallax_shadows"] = (props.flags & PARALLAX_SHADOW_ENABLED_FLAG) != 0;

				j["diffuse_textname"] = relative(texture_names.diffuse_texname);
				j["high_textname"] = relative(texture_names.high_textname);
				j["normal_textname"] = relative(texture_names.normal_textname);
				j["spec_textname"] = relative(texture_names.spec_textname);
				j["ao_textname"] = relative(texture_names.ao_textname);
				j["arm_textname"] = relative(texture_names.arm_textname);
				j["emission_textname"] = relative(texture_names.emission_textname);
				j["opacity_textname"] = relative(texture_names.opacity_textname);

				j["draw_vs"] = shader_names.draw_vs;
				j["draw_hs"] = shader_names.draw_hs;
				j["draw_ds"] = shader_names.draw_ds;
				j["draw_gs"] = shader_names.draw_gs;
				j["draw_ps"] = shader_names.draw_ps;
				j["shadow_vs"] = shader_names.shadow_vs;
				j["shadow_gs"] = shader_names.shadow_gs;
				j["depth_vs"] = shader_names.depth_vs;
				j["depth_ps"] = shader_names.depth_ps;

				return j;
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