#include "MaterialPanel.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "MaterialPreview.h"

#include "imgui.h"
#include <World.h>
#include <Components/Base.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace MaterialOps {
		namespace {

			//The stand-in assets World creates on demand. They are engine bookkeeping,
			//not authored content: they belong to no .mat file and must not be listed,
			//edited or saved.
			bool IsInternal(const std::string& name) {
				return name.rfind("__default_", 0) == 0;
			}

			Core::MaterialData* Find(EditorState& state, const std::string& name) {
				if (state.world == nullptr) {
					return nullptr;
				}
				if (state.world->IsMaterialRemoved(name)) {
					return nullptr;
				}
				return state.world->GetMaterials().Get(name);
			}

			//Marks the file a material belongs to as having unsaved changes, and drops
			//its cached thumbnail so the next panel frame redraws it. Every mutation
			//path has to go through here - a missed call leaves the panel showing a
			//stale sphere and File/Save Materials skipping a genuinely changed file.
			void Touch(EditorState& state, const std::string& material_name) {
				const std::string file = state.world->GetMaterialOrigin(material_name);
				if (!file.empty()) {
					state.dirty_material_files.insert(file);
				}
				MaterialPreview::Invalidate(material_name);
			}

			//Compiled shaders found next to the executable, per pipeline stage. Cached
			//because ListShaders hits the filesystem and the panel calls it once per
			//shader row per frame.
			std::map<std::string, std::vector<std::string>> shader_cache;

			bool SameShaders(const Core::MaterialShaderNames& a, const Core::MaterialShaderNames& b) {
				return a.draw_vs == b.draw_vs && a.draw_hs == b.draw_hs &&
					a.draw_ds == b.draw_ds && a.draw_gs == b.draw_gs &&
					a.draw_ps == b.draw_ps && a.shadow_vs == b.shadow_vs &&
					a.shadow_gs == b.shadow_gs && a.depth_vs == b.depth_vs &&
					a.depth_ps == b.depth_ps;
			}

			bool SameSnapshot(const MaterialSnapshot& a, const MaterialSnapshot& b) {
				return SameShaders(a.shader_names, b.shader_names) &&
					std::memcmp(&a.props, &b.props, sizeof(Core::MaterialProps)) == 0 &&
					a.texture_names.diffuse_texname == b.texture_names.diffuse_texname &&
					a.texture_names.normal_textname == b.texture_names.normal_textname &&
					a.texture_names.high_textname == b.texture_names.high_textname &&
					a.texture_names.spec_textname == b.texture_names.spec_textname &&
					a.texture_names.ao_textname == b.texture_names.ao_textname &&
					a.texture_names.arm_textname == b.texture_names.arm_textname &&
					a.texture_names.emission_textname == b.texture_names.emission_textname &&
					a.texture_names.opacity_textname == b.texture_names.opacity_textname &&
					a.tessellation_factor == b.tessellation_factor &&
					a.displacement_scale == b.displacement_scale &&
					a.tessellation_type == b.tessellation_type;
			}
		}

		std::vector<std::string> ListMaterials(EditorState& state) {
			std::vector<std::string> names;
			if (state.world == nullptr) {
				return names;
			}
			for (const auto& material : state.world->GetMaterials().GetData()) {
				if (!material.name.empty() && !IsInternal(material.name) &&
					!state.world->IsMaterialRemoved(material.name)) {
					names.push_back(material.name);
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		bool GetSnapshot(EditorState& state, const std::string& material_name, MaterialSnapshot& out) {
			Core::MaterialData* m = Find(state, material_name);
			if (m == nullptr) {
				return false;
			}
			out.props = m->props;
			out.texture_names = m->texture_names;
			out.shader_names = m->shader_names;
			out.tessellation_factor = m->tessellation_factor;
			out.displacement_scale = m->displacement_scale;
			out.tessellation_type = m->tessellation_type;
			return true;
		}

		bool ApplySnapshot(EditorState& state, const std::string& material_name,
			const MaterialSnapshot& snapshot, std::string& error) {
			Core::MaterialData* m = Find(state, material_name);
			if (m == nullptr) {
				error = "material not found: " + material_name;
				return false;
			}
			const bool textures_changed =
				m->texture_names.diffuse_texname != snapshot.texture_names.diffuse_texname ||
				m->texture_names.normal_textname != snapshot.texture_names.normal_textname ||
				m->texture_names.high_textname != snapshot.texture_names.high_textname ||
				m->texture_names.spec_textname != snapshot.texture_names.spec_textname ||
				m->texture_names.ao_textname != snapshot.texture_names.ao_textname ||
				m->texture_names.arm_textname != snapshot.texture_names.arm_textname ||
				m->texture_names.emission_textname != snapshot.texture_names.emission_textname ||
				m->texture_names.opacity_textname != snapshot.texture_names.opacity_textname;

			//Shaders go through the world, which also re-registers the entities using
			//this material with the render system (the draw trees are keyed by shader).
			if (!SameShaders(m->shader_names, snapshot.shader_names)) {
				state.world->SetMaterialShaders(material_name, snapshot.shader_names);
			}
			m->props = snapshot.props;
			m->texture_names = snapshot.texture_names;
			m->tessellation_factor = snapshot.tessellation_factor;
			m->displacement_scale = snapshot.displacement_scale;
			m->tessellation_type = snapshot.tessellation_type;
			if (textures_changed) {
				//Init() reloads every map from texture_names and recomputes the
				//*_MAP_ENABLED flags, which props alone cannot express correctly.
				m->Init();
			}
			Touch(state, material_name);
			return true;
		}

		void RecordEdit(EditorState& state, const std::string& material_name,
			const MaterialSnapshot& before) {
			MaterialSnapshot after;
			if (!GetSnapshot(state, material_name, after)) {
				return;
			}
			if (SameSnapshot(before, after)) {
				return; //a drag that ended where it started records nothing
			}
			Touch(state, material_name);

			const std::string name = material_name;
			EditorHistory::Push({
				"edit material " + name,
				[name, before](EditorState& s) {
					std::string error;
					ApplySnapshot(s, name, before, error);
				},
				[name, after](EditorState& s) {
					std::string error;
					ApplySnapshot(s, name, after, error);
				} });
		}

		bool CreateMaterial(EditorState& state, const std::string& name,
			const std::string& mat_file, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (name.empty()) {
				error = "material name is empty";
				return false;
			}
			if (IsInternal(name)) {
				error = "'__default_' is reserved for engine stand-in assets";
				return false;
			}
			if (Find(state, name) != nullptr) {
				error = "material already exists: " + name;
				return false;
			}
			if (state.world->CreateMaterial(name, mat_file) == nullptr) {
				error = "could not create material in " + mat_file;
				return false;
			}
			state.dirty_material_files.insert(mat_file);
			state.selected_material = name;
			MaterialPreview::Invalidate(name);

			const std::string created = name;
			const std::string file = mat_file;
			EditorHistory::Push({
				"create material " + created,
				[created, file](EditorState& s) {
					s.world->RemoveMaterial(created);
					s.dirty_material_files.insert(file);
					if (s.selected_material == created) {
						s.selected_material.clear();
					}
				},
				[created, file](EditorState& s) {
					//CreateMaterial revives a retired name in place, so redo gets the
					//same MaterialData back rather than a second one.
					s.world->CreateMaterial(created, file);
					s.dirty_material_files.insert(file);
					s.selected_material = created;
					MaterialPreview::Invalidate(created);
				} });
			return true;
		}

		bool DuplicateMaterial(EditorState& state, const std::string& source_name,
			const std::string& new_name, std::string& error) {
			MaterialSnapshot source;
			if (!GetSnapshot(state, source_name, source)) {
				error = "material not found: " + source_name;
				return false;
			}
			const std::string file = state.world->GetMaterialOrigin(source_name);
			if (file.empty()) {
				error = source_name + " belongs to no .mat file and cannot be duplicated";
				return false;
			}
			if (!CreateMaterial(state, new_name, file, error)) {
				return false;
			}
			//The copy starts as plain white; push the source's values onto it. Recorded
			//as its own history step, so undoing twice removes the material entirely.
			MaterialSnapshot blank;
			GetSnapshot(state, new_name, blank);
			if (!ApplySnapshot(state, new_name, source, error)) {
				return false;
			}
			RecordEdit(state, new_name, blank);
			return true;
		}

		std::vector<std::string> FindUsers(EditorState& state, const std::string& material_name) {
			std::vector<std::string> users;
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			Core::MaterialData* target = Find(state, material_name);
			if (c == nullptr || target == nullptr) {
				return users;
			}
			for (const auto& entry : c->GetEntites()) {
				const Entity e = entry.second;
				if (c->ContainsComponent<Material>(e) &&
					c->GetComponent<Material>(e).data == target) {
					users.push_back(entry.first);
				}
			}
			std::sort(users.begin(), users.end());
			return users;
		}

		bool AssignMaterial(EditorState& state, const std::string& entity_name,
			const std::string& material_name, std::string& error) {
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			if (c == nullptr) {
				error = "no scene loaded";
				return false;
			}
			const Entity e = c->GetEntityByName(entity_name);
			if (e == INVALID_ENTITY_ID) {
				error = "entity not found: " + entity_name;
				return false;
			}
			if (!c->ContainsComponent<Material>(e)) {
				error = entity_name + " has no Material component";
				return false;
			}
			Core::MaterialData* current = c->GetComponent<Material>(e).data;
			const std::string previous = (current != nullptr) ? current->name : std::string();
			if (previous == material_name) {
				return true; //no-op, nothing to record
			}
			if (!state.world->SetEntityMaterial(e, material_name)) {
				error = "material not found: " + material_name;
				return false;
			}

			//Record the assignment as a Material component block so it survives a save
			//and reload; Material::FromJson resolves the name against the world.
			auto record_delta = [](EditorState& s, const std::string& entity, const std::string& material) {
				ComponentDelta& delta = s.component_deltas[entity];
				delta.removed.erase(Material::NAME);
				delta.added[Material::NAME] = nlohmann::json{ {"name", material} };
			};
			record_delta(state, entity_name, material_name);

			const std::string entity = entity_name;
			const std::string assigned = material_name;
			EditorHistory::Push({
				"assign " + assigned + " to " + entity,
				[entity, previous, record_delta](EditorState& s) {
					Coordinator* co = s.world->GetCoordinator();
					const Entity ent = co->GetEntityByName(entity);
					if (ent != INVALID_ENTITY_ID && !previous.empty()) {
						s.world->SetEntityMaterial(ent, previous);
						record_delta(s, entity, previous);
					}
				},
				[entity, assigned, record_delta](EditorState& s) {
					Coordinator* co = s.world->GetCoordinator();
					const Entity ent = co->GetEntityByName(entity);
					if (ent != INVALID_ENTITY_ID) {
						s.world->SetEntityMaterial(ent, assigned);
						record_delta(s, entity, assigned);
					}
				} });
			return true;
		}

		bool RemoveMaterial(EditorState& state, const std::string& name, std::string& error) {
			if (Find(state, name) == nullptr) {
				error = "material not found: " + name;
				return false;
			}
			const std::string file = state.world->GetMaterialOrigin(name);
			Core::MaterialData* fallback = state.world->GetDefaultMaterial();
			if (fallback == nullptr) {
				error = "no default material available to reassign users to";
				return false;
			}
			//Repoint the users first: World::RemoveMaterial deliberately leaves the
			//MaterialData alive, but a retired material must not stay in use.
			const std::vector<std::string> users = FindUsers(state, name);
			Coordinator* c = state.world->GetCoordinator();
			for (const std::string& user : users) {
				const Entity e = c->GetEntityByName(user);
				if (e != INVALID_ENTITY_ID) {
					state.world->SetEntityMaterial(e, World::DEFAULT_MATERIAL_NAME);
					ComponentDelta& delta = state.component_deltas[user];
					delta.removed.erase(Material::NAME);
					delta.added[Material::NAME] = nlohmann::json{ {"name", World::DEFAULT_MATERIAL_NAME} };
				}
			}
			state.world->RemoveMaterial(name);
			if (!file.empty()) {
				state.dirty_material_files.insert(file);
			}
			if (state.selected_material == name) {
				state.selected_material.clear();
			}

			const std::string removed = name;
			const std::string origin = file;
			EditorHistory::Push({
				"remove material " + removed,
				[removed, origin, users](EditorState& s) {
					if (!s.world->RestoreMaterial(removed, origin)) {
						return;
					}
					Coordinator* co = s.world->GetCoordinator();
					for (const std::string& user : users) {
						const Entity e = co->GetEntityByName(user);
						if (e != INVALID_ENTITY_ID) {
							s.world->SetEntityMaterial(e, removed);
							ComponentDelta& delta = s.component_deltas[user];
							delta.added[Material::NAME] = nlohmann::json{ {"name", removed} };
						}
					}
					if (!origin.empty()) {
						s.dirty_material_files.insert(origin);
					}
					MaterialPreview::Invalidate(removed);
				},
				[removed, origin, users](EditorState& s) {
					Coordinator* co = s.world->GetCoordinator();
					for (const std::string& user : users) {
						const Entity e = co->GetEntityByName(user);
						if (e != INVALID_ENTITY_ID) {
							s.world->SetEntityMaterial(e, World::DEFAULT_MATERIAL_NAME);
							ComponentDelta& delta = s.component_deltas[user];
							delta.added[Material::NAME] =
								nlohmann::json{ {"name", World::DEFAULT_MATERIAL_NAME} };
						}
					}
					s.world->RemoveMaterial(removed);
					if (!origin.empty()) {
						s.dirty_material_files.insert(origin);
					}
					if (s.selected_material == removed) {
						s.selected_material.clear();
					}
				} });
			return true;
		}

		const std::vector<std::string>& ListShaders(const std::string& stage_suffix) {
			auto it = shader_cache.find(stage_suffix);
			if (it != shader_cache.end()) {
				return it->second;
			}
			std::vector<std::string>& names = shader_cache[stage_suffix];
			//Shaders are compiled next to the executable, which is also the working
			//directory the editor is launched from.
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
				if (ec || !entry.is_regular_file()) {
					continue;
				}
				const std::string file = entry.path().filename().string();
				const std::string stem = entry.path().stem().string();
				if (entry.path().extension() != ".cso") {
					continue;
				}
				//"MainRenderVS.cso" -> stem "MainRenderVS" ends with "VS". This is the
				//engine's convention for every shader in the tree, and it is what makes
				//the picker safe (see the header).
				if (stem.size() >= stage_suffix.size() &&
					stem.compare(stem.size() - stage_suffix.size(), stage_suffix.size(),
						stage_suffix) == 0) {
					names.push_back(file);
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		void RefreshShaderList() {
			shader_cache.clear();
		}

		bool SetShaders(EditorState& state, const std::string& material_name,
			const Core::MaterialShaderNames& names, std::string& error) {
			MaterialSnapshot before;
			if (!GetSnapshot(state, material_name, before)) {
				error = "material not found: " + material_name;
				return false;
			}
			if (SameShaders(before.shader_names, names)) {
				return true; //no-op, nothing to record
			}
			if (!state.world->SetMaterialShaders(material_name, names)) {
				error = "one or more shaders could not be loaded for the stage they were "
					"given for; the material is unchanged";
				return false;
			}
			RecordEdit(state, material_name, before);
			return true;
		}

		bool SaveMaterials(EditorState& state, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (state.dirty_material_files.empty()) {
				state.status_message = "No material changes to save";
				return true;
			}
			std::vector<std::string> failed;
			std::set<std::string> saved;
			for (const std::string& file : state.dirty_material_files) {
				if (state.world->SaveMaterialFile(file)) {
					saved.insert(file);
				}
				else {
					failed.push_back(file);
				}
			}
			//Only clear what actually made it to disk, so a retry saves the rest.
			for (const std::string& file : saved) {
				state.dirty_material_files.erase(file);
			}
			if (!failed.empty()) {
				error = "could not write " + failed.front() +
					(failed.size() > 1 ? " (and " + std::to_string(failed.size() - 1) + " more)" : "");
				return false;
			}
			state.status_message = "Saved " + std::to_string(saved.size()) + " material file(s)";
			return true;
		}

		bool HasUnsavedMaterials(const EditorState& state) {
			return !state.dirty_material_files.empty();
		}
	}

	namespace MaterialPanel {
		namespace {

			constexpr int THUMBNAIL_SIZE = 48;
			constexpr int PREVIEW_SIZE = 160;

			//Coalesced drag state, same shape as the Inspector's transform editing: the
			//pre-edit snapshot is taken when a widget is grabbed and the history action
			//is pushed when it is released, so dragging a slider records one step
			//instead of one per frame.
			MaterialOps::MaterialSnapshot pending_before;
			bool pending_valid = false;

			void DrawThumbnail(Core::MaterialData* material, int size) {
				ImTextureID texture = MaterialPreview::Get(material, size);
				if (texture != (ImTextureID)nullptr) {
					ImGui::Image(texture, ImVec2((float)size, (float)size));
				}
				else {
					//No preview pass available (missing shaders, or no device yet):
					//fall back to a flat diffuse swatch rather than an empty hole.
					const float4& c = material->props.diffuseColor;
					ImGui::ColorButton("##swatch", ImVec4(c.x, c.y, c.z, 1.0f),
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
						ImVec2((float)size, (float)size));
				}
			}

			//One row of the material list: thumbnail, name, and the file it belongs to.
			void DrawListEntry(EditorState& state, const std::string& name) {
				Core::MaterialData* material = state.world->GetMaterials().Get(name);
				if (material == nullptr) {
					return;
				}
				ImGui::PushID(name.c_str());
				ImGui::BeginGroup();
				DrawThumbnail(material, THUMBNAIL_SIZE);
				ImGui::SameLine();

				ImGui::BeginGroup();
				const bool selected = (state.selected_material == name);
				if (ImGui::Selectable(name.c_str(), selected)) {
					state.selected_material = name;
				}
				const std::string file = state.world->GetMaterialOrigin(name);
				ImGui::TextDisabled("%s%s", file.empty() ? "(no file)" : file.c_str(),
					state.dirty_material_files.count(file) != 0 ? " *" : "");
				ImGui::EndGroup();
				ImGui::EndGroup();
				ImGui::PopID();
			}

			//New material: a name plus the .mat file it should live in. Duplicate does
			//not come through here - it derives both from its source and runs in one
			//click, so a modal would only be in the way.
			void DrawCreateModal(EditorState& state) {
				static char name_buf[128] = "";
				static int file_index = 0;

				if (ImGui::BeginPopupModal("Create Material", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
					std::vector<std::string> files;
					for (const auto& entry : state.world->GetMaterialFiles()) {
						files.push_back(entry.first);
					}
					if (files.empty()) {
						ImGui::TextWrapped("This level declares no material files, so there is "
							"nowhere to put a new material. Add a \"material_files\" entry to "
							"the level first.");
						if (ImGui::Button("Close")) {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
						return;
					}
					//Parenthesized: windows.h defines a min() macro that would otherwise
					//eat the call.
					file_index = (std::min)(file_index, (int)files.size() - 1);

					ImGui::InputText("Name", name_buf, sizeof(name_buf));
					if (ImGui::BeginCombo("File", files[file_index].c_str())) {
						for (int i = 0; i < (int)files.size(); ++i) {
							if (ImGui::Selectable(files[i].c_str(), i == file_index)) {
								file_index = i;
							}
						}
						ImGui::EndCombo();
					}

					if (ImGui::Button("Create")) {
						std::string error;
						if (MaterialOps::CreateMaterial(state, name_buf, files[file_index], error)) {
							name_buf[0] = '\0';
							ImGui::CloseCurrentPopup();
						}
						else {
							state.status_message = "Create material failed: " + error;
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			//A read-only texture slot plus a text field for its path. Textures are
			//referenced by path rather than picked from a browser: the .mat format
			//stores a name relative to the file's own texture root, and the editor has
			//no way to copy an arbitrary file into that root.
			bool DrawTextureRow(const char* label, std::string& path) {
				char buffer[512];
				snprintf(buffer, sizeof(buffer), "%s", path.c_str());
				ImGui::PushID(label);
				const bool changed = ImGui::InputText(label, buffer, sizeof(buffer),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (changed) {
					path = buffer;
				}
				ImGui::PopID();
				return changed;
			}

		}

		void DrawMaterialProperties(EditorState& state, const std::string& name,
			bool show_preview) {
			Core::MaterialData* m = state.world->GetMaterials().Get(name);
			if (m == nullptr) {
				ImGui::TextDisabled("(material not found)");
				return;
			}

			if (show_preview) {
				DrawThumbnail(m, PREVIEW_SIZE);
			}
			ImGui::Text("%s", m->name.c_str());
			const std::string file = state.world->GetMaterialOrigin(name);
			ImGui::TextDisabled("File: %s%s", file.empty() ? "(none)" : file.c_str(),
				state.dirty_material_files.count(file) != 0 ? "  (unsaved)" : "");
			ImGui::Separator();

			MaterialOps::MaterialSnapshot frame_before;
			MaterialOps::GetSnapshot(state, name, frame_before);
			Core::MaterialProps& p = m->props;

			bool changed = false;
			bool activated = false;
			bool finished = false;
			auto track = [&](bool widget_changed) {
				changed |= widget_changed;
				activated |= ImGui::IsItemActivated();
				finished |= ImGui::IsItemDeactivatedAfterEdit();
			};

			track(ImGui::ColorEdit4("Diffuse", &p.diffuseColor.x));
			track(ImGui::DragFloat("Specular", &p.specIntensity, 0.01f, 0.0f, 16.0f));
			track(ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f));
			track(ImGui::DragFloat("Density", &p.density, 0.01f, 0.0f, 10.0f));
			track(ImGui::DragFloat("Emission", &p.emission, 0.01f, 0.0f, 100.0f));
			track(ImGui::ColorEdit3("Emissive", &p.emission_color.x));
			track(ImGui::DragFloat("Bloom", &p.bloom_scale, 0.01f, 0.0f, 10.0f));
			track(ImGui::DragFloat("RT reflex", &p.rt_reflex, 0.01f, 0.0f, 1.0f));
			track(ImGui::DragFloat("Parallax", &p.parallax_scale, 0.01f));
			track(ImGui::DragFloat("Parallax steps", &p.parallax_steps, 1.0f, 0.0f, 64.0f));
			track(ImGui::DragFloat("Parallax angles", &p.parallax_angle_steps, 1.0f, 0.0f, 64.0f));
			track(ImGui::DragFloat("Displace", &m->displacement_scale, 0.01f));
			track(ImGui::DragFloat("Tessellate", &m->tessellation_factor, 0.1f, 0.0f, 64.0f));

			//The four flags the .mat file actually carries; the rest of props.flags
			//is derived from which texture maps are present (MaterialData::Init).
			auto flag_checkbox = [&](const char* label, unsigned int flag) {
				bool on = (p.flags & flag) != 0;
				if (ImGui::Checkbox(label, &on)) {
					on ? (p.flags |= flag) : (p.flags &= ~flag);
					changed = true;
					finished = true; //a checkbox is one discrete edit, not a drag
				}
			};
			flag_checkbox("Ray tracing", RAY_TRACING_ENABLED_FLAG);
			flag_checkbox("Alpha", ALPHA_ENABLED_FLAG);
			flag_checkbox("Blend", BLEND_ENABLED_FLAG);
			flag_checkbox("Parallax shadows", PARALLAX_SHADOW_ENABLED_FLAG);

			if (activated && !pending_valid) {
				pending_before = frame_before;
				pending_valid = true;
			}
			if (changed) {
				//Redraw the sphere as the value moves, so dragging is a live preview.
				MaterialPreview::Invalidate(name);
			}
			if (finished && pending_valid) {
				MaterialOps::RecordEdit(state, name, pending_before);
				pending_valid = false;
			}

			if (ImGui::CollapsingHeader("Textures")) {
				ImGui::TextDisabled("Absolute paths. Press Enter to apply.");
				Core::MaterialTextures& t = m->texture_names;
				MaterialOps::MaterialSnapshot before_textures;
				MaterialOps::GetSnapshot(state, name, before_textures);
				bool texture_changed = false;
				texture_changed |= DrawTextureRow("Diffuse map", t.diffuse_texname);
				texture_changed |= DrawTextureRow("Normal map", t.normal_textname);
				texture_changed |= DrawTextureRow("Height map", t.high_textname);
				texture_changed |= DrawTextureRow("Specular map", t.spec_textname);
				texture_changed |= DrawTextureRow("AO map", t.ao_textname);
				texture_changed |= DrawTextureRow("ARM map", t.arm_textname);
				texture_changed |= DrawTextureRow("Emission map", t.emission_textname);
				texture_changed |= DrawTextureRow("Opacity map", t.opacity_textname);
				if (texture_changed) {
					//Reload the maps and recompute the *_MAP_ENABLED flags.
					m->Init();
					MaterialOps::RecordEdit(state, name, before_textures);
				}
			}

			if (ImGui::CollapsingHeader("Shaders")) {
				//The stages the .mat file carries. Each is a picker over the compiled
				//.cso files for that stage - see MaterialOps::ListShaders for why this
				//is not a free-text field.
				Core::MaterialShaderNames names = m->shader_names;
				bool shader_changed = false;
				auto shader_row = [&](const char* label, const char* stage,
					std::string& value) {
					const std::vector<std::string>& options = MaterialOps::ListShaders(stage);
					ImGui::PushID(label);
					if (ImGui::BeginCombo(label, value.c_str())) {
						for (const std::string& option : options) {
							if (ImGui::Selectable(option.c_str(), option == value) &&
								option != value) {
								value = option;
								shader_changed = true;
							}
						}
						if (options.empty()) {
							ImGui::TextDisabled("(no %s shaders found)", stage);
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();
				};

				ImGui::TextDisabled("Draw pass");
				shader_row("Vertex", "VS", names.draw_vs);
				shader_row("Hull", "HS", names.draw_hs);
				shader_row("Domain", "DS", names.draw_ds);
				shader_row("Geometry", "GS", names.draw_gs);
				shader_row("Pixel", "PS", names.draw_ps);
				ImGui::TextDisabled("Shadow pass");
				shader_row("Shadow vertex", "VS", names.shadow_vs);
				shader_row("Shadow geometry", "GS", names.shadow_gs);
				ImGui::TextDisabled("Depth pass");
				shader_row("Depth vertex", "VS", names.depth_vs);
				shader_row("Depth pixel", "PS", names.depth_ps);

				if (shader_changed) {
					std::string error;
					if (!MaterialOps::SetShaders(state, name, names, error)) {
						state.status_message = "Set shaders failed: " + error;
					}
				}
				if (ImGui::SmallButton("Rescan shader files")) {
					MaterialOps::RefreshShaderList();
				}
			}

			if (ImGui::CollapsingHeader("Used by")) {
				const std::vector<std::string> users = MaterialOps::FindUsers(state, name);
				if (users.empty()) {
					ImGui::TextDisabled("(no entities)");
				}
				for (const std::string& user : users) {
					ImGui::BulletText("%s", user.c_str());
				}
			}
		}

		void Draw(EditorState& state) {
			if (state.world == nullptr) {
				return;
			}
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			const ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(
				ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.25f, vp->WorkPos.y + vp->WorkSize.y * 0.15f), cond);
			ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.45f, vp->WorkSize.y * 0.60f), cond);

			if (!ImGui::Begin(EditorLayout::MATERIALS_WINDOW, &state.show_material_panel)) {
				ImGui::End();
				return;
			}

			if (ImGui::Button("New...")) {
				ImGui::OpenPopup("Create Material");
			}
			ImGui::SameLine();
			const bool has_selection = !state.selected_material.empty();
			ImGui::BeginDisabled(!has_selection);
			if (ImGui::Button("Duplicate")) {
				std::string error;
				//Name the copy after its source; the user can rename it afterwards by
				//duplicating again, which keeps this a one-click operation.
				std::string candidate = state.selected_material + "_copy";
				int suffix = 2;
				while (state.world->GetMaterials().Get(candidate) != nullptr &&
					!state.world->IsMaterialRemoved(candidate)) {
					candidate = state.selected_material + "_copy" + std::to_string(suffix++);
				}
				if (!MaterialOps::DuplicateMaterial(state, state.selected_material, candidate, error)) {
					state.status_message = "Duplicate failed: " + error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				ImGui::OpenPopup("Remove Material");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!MaterialOps::HasUnsavedMaterials(state));
			if (ImGui::Button("Save Materials")) {
				std::string error;
				if (!MaterialOps::SaveMaterials(state, error)) {
					state.status_message = "Save materials failed: " + error;
				}
			}
			ImGui::EndDisabled();
			if (MaterialOps::HasUnsavedMaterials(state)) {
				ImGui::SameLine();
				ImGui::TextDisabled("(%d file(s) unsaved)", (int)state.dirty_material_files.size());
			}

			DrawCreateModal(state);

			if (ImGui::BeginPopupModal("Remove Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				const std::vector<std::string> users = MaterialOps::FindUsers(state, state.selected_material);
				ImGui::Text("Remove material '%s'?", state.selected_material.c_str());
				if (!users.empty()) {
					ImGui::TextWrapped("%d entit%s using it will be reassigned to the default "
						"white material.", (int)users.size(), users.size() == 1 ? "y is" : "ies are");
				}
				if (ImGui::Button("Remove")) {
					std::string error;
					if (!MaterialOps::RemoveMaterial(state, state.selected_material, error)) {
						state.status_message = "Remove failed: " + error;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::Separator();

			const std::vector<std::string> names = MaterialOps::ListMaterials(state);
			if (names.empty()) {
				ImGui::TextDisabled("This level has no materials.");
				ImGui::End();
				return;
			}

			//List on the left, the selected material's properties on the right.
			const float list_width = ImGui::GetContentRegionAvail().x * 0.42f;
			if (ImGui::BeginChild("##material_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_Border)) {
				for (const std::string& name : names) {
					DrawListEntry(state, name);
				}
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("##material_props", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
				if (state.selected_material.empty()) {
					ImGui::TextDisabled("Select a material to edit it.");
				}
				else {
					DrawMaterialProperties(state, state.selected_material, true);
				}
			}
			ImGui::EndChild();
			ImGui::End();
		}
	}
}
