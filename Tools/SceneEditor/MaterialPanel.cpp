#include "MaterialPanel.h"
#include "MultiMaterialPanel.h"
#include "PolyHaven.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "MaterialPreview.h"

#include "imgui.h"
#include <World.h>
#include <Components/Base.h>
#include <Core/SimpleShader.h>

#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

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
			//<project>/Assets/Shaders once a project is open (RegisterProjectShaderFolder);
			//scanned by ListShaders beside the executable's own directory.
			std::string project_shader_dir;

			//Texture picker cache: every image under <project>/Assets/Textures, relative
			//to it. Same reason as shader_cache - the panel asks once per row per frame.
			std::vector<std::string> texture_cache;
			bool texture_cache_valid = false;

			//Compile profile for each pipeline stage suffix - mirrors
			//ISimpleShader::GetShaderProfile() for the five stages a material picks.
			std::string ProfileForStage(const std::string& stage) {
				if (stage == "VS") return "vs_5_0";
				if (stage == "HS") return "hs_5_0";
				if (stage == "DS") return "ds_5_0";
				if (stage == "GS") return "gs_5_0";
				if (stage == "PS") return "ps_5_0";
				return {};
			}

			//A working minimal shader, for the two stages simple enough to have one.
			//HS/DS/GS need a real starting point - a patch-constant function, a
			//topology declaration - that only makes sense copied from an existing
			//shader (see CreateShaderFile), so they have no fallback here.
			std::string MinimalShaderTemplate(const std::string& stage) {
				if (stage == "VS") {
					return
						"// New vertex shader - replace this. See MainRender/MainRenderVS.hlsl\n"
						"// for the layout the standard draw pipeline expects.\n"
						"float4 main(float4 position : POSITION) : SV_POSITION\n"
						"{\n"
						"\treturn position;\n"
						"}\n";
				}
				if (stage == "PS") {
					return
						"// New pixel shader - replace this. See MainRender/MainRenderPS.hlsl\n"
						"// for the material/lighting cbuffer the standard draw pipeline binds.\n"
						"float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
						"{\n"
						"\treturn float4(1.0f, 0.0f, 1.0f, 1.0f); // magenta: replace me\n"
						"}\n";
				}
				return {};
			}

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

		const std::vector<TessMode>& TessellationModes() {
			static const std::vector<TessMode> modes = {
				{ "off", 0, "No tessellation." },
				{ "on", 3, "The factor as it is, at every distance and angle." },
				{ "distance", 2, "More triangles the closer the surface is." },
				{ "silhouette", 1, "More triangles toward the edges of the shape." } };
			return modes;
		}

		const char* TessellationModeName(int type) {
			for (const TessMode& mode : TessellationModes()) {
				if (mode.type == type) {
					return mode.name;
				}
			}
			return "off";
		}

		bool TessellationModeFromName(const std::string& text, int& type) {
			std::string lower = text;
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			for (const TessMode& mode : TessellationModes()) {
				if (lower == mode.name || lower == std::to_string(mode.type)) {
					type = mode.type;
					return true;
				}
			}
			return false;
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

		//A material authored inside an imported model has no .mat file of its own
		//(GetMaterialOrigin returns ""), and MaterialOps::Touch silently skips marking
		//anything dirty for one - there is nowhere to save it. This is how it gets one:
		//moves it into an existing level material file so future edits, and this one,
		//become part of what File/Save Materials (and Save Level) writes to disk.
		bool AssignMaterialFile(EditorState& state, const std::string& name,
			const std::string& mat_file, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (!state.world->GetMaterialOrigin(name).empty()) {
				error = name + " already belongs to a file";
				return false;
			}
			if (!state.world->SetMaterialOrigin(name, mat_file)) {
				error = "could not assign " + name + " to " + mat_file;
				return false;
			}
			state.dirty_material_files.insert(mat_file);
			MaterialPreview::Invalidate(name);

			const std::string assigned = name;
			const std::string file = mat_file;
			EditorHistory::Push({
				"move material " + assigned + " to " + file,
				[assigned](EditorState& s) {
					s.world->ClearMaterialOrigin(assigned);
				},
				[assigned, file](EditorState& s) {
					s.world->SetMaterialOrigin(assigned, file);
					s.dirty_material_files.insert(file);
				} });
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
			//The engine's own compiled shaders, then the open project's Assets/Shaders.
			std::vector<std::string> dirs = { "." };
			if (!project_shader_dir.empty()) {
				dirs.push_back(project_shader_dir);
			}
			for (const std::string& dir : dirs) {
				for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
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
			}
			std::sort(names.begin(), names.end());
			names.erase(std::unique(names.begin(), names.end()), names.end());
			return names;
		}

		void RefreshShaderList() {
			shader_cache.clear();
		}

		std::string ShadersDir(const EditorState& state) {
			return state.project_root.empty() ? std::string()
				: (std::filesystem::path(state.project_root) / "Assets" / "Shaders").make_preferred().string();
		}

		void RegisterProjectShaderFolder(EditorState& state) {
			const std::string dir = ShadersDir(state);
			if (dir.empty()) {
				return;
			}
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			project_shader_dir = dir;
			//Binaries: so a material naming "Foo.cso" finds Assets/Shaders/Foo.cso.
			//Sources: so the Edit button and hot reload resolve the .hlsl beside it.
			Core::ShaderFactory::Get()->AddBinaryFolder(dir);
			Core::ShaderCompiler::Get()->AddSourceFolder(dir);
			Core::ShaderFactory::Get()->ForgetSourcePaths();
			RefreshShaderList();
		}

		bool ImportShader(EditorState& state, const std::string& source_path,
			std::string& out_cso_name, std::string& error) {
			namespace fs = std::filesystem;
			const std::string dir = ShadersDir(state);
			if (dir.empty()) {
				error = "no project open - shaders are imported into <project>/Assets/Shaders";
				return false;
			}
			std::error_code ec;
			if (!fs::is_regular_file(source_path, ec)) {
				error = "file not found: " + source_path;
				return false;
			}
			const fs::path src(source_path);
			std::string ext = src.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			if (ext != ".hlsl" && ext != ".cso") {
				error = "a shader is a .hlsl source or a compiled .cso";
				return false;
			}
			const std::string stem = src.stem().string();
			std::string stage;
			for (const char* s : { "VS", "HS", "DS", "GS", "PS" }) {
				const std::string suffix = s;
				if (stem.size() > suffix.size() &&
					stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
					stage = suffix;
				}
			}
			if (stage.empty()) {
				error = "name must end with VS, HS, DS, GS or PS - the picker relies on the "
					"engine's naming convention to tell shaders of different stages apart";
				return false;
			}
			RegisterProjectShaderFolder(state);
			const fs::path dest = fs::path(dir) / src.filename();
			if (!fs::exists(dest, ec) || !fs::equivalent(src, dest, ec)) {
				fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
				if (ec) {
					error = "could not copy into " + dest.string() + ": " + ec.message();
					return false;
				}
			}
			const std::string cso_name = stem + ".cso";
			if (ext == ".hlsl") {
				//The original's folder is searched for this compile only, so quoted
				//#includes that sat next to it still resolve.
				Core::ShaderCompiler::Result result;
				if (!Core::ShaderCompiler::Get()->CompileToFile(dest.string(), ProfileForStage(stage),
					(fs::path(dir) / cso_name).string(), result, { src.parent_path().string() })) {
					error = "compile failed: " + result.error;
					return false;
				}
				if (result.blob != nullptr) {
					result.blob->Release();
				}
				Core::ShaderCompiler::Get()->AddSourceFolder(dir);
				Core::ShaderFactory::Get()->ForgetSourcePaths();
			}
			RefreshShaderList();
			out_cso_name = cso_name;
			state.status_message = "Imported shader " + cso_name + " into Assets/Shaders";
			return true;
		}

		std::string TexturesDir(const EditorState& state) {
			return state.project_root.empty() ? std::string()
				: (std::filesystem::path(state.project_root) / "Assets" / "Textures").make_preferred().string();
		}

		static bool IsImageFile(const std::filesystem::path& p) {
			std::string ext = p.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			return ext == ".dds" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
				ext == ".bmp" || ext == ".tif" || ext == ".tiff";
		}

		const std::vector<std::string>& ListTextures(const EditorState& state) {
			if (!texture_cache_valid) {
				texture_cache.clear();
				const std::string dir = TexturesDir(state);
				std::error_code ec;
				if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
					for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
						!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
						if (it->is_regular_file() && IsImageFile(it->path())) {
							texture_cache.push_back(std::filesystem::relative(it->path(), dir, ec)
								.make_preferred().string());
						}
					}
				}
				std::sort(texture_cache.begin(), texture_cache.end());
				texture_cache_valid = true;
			}
			return texture_cache;
		}

		void RefreshTextureList() {
			texture_cache_valid = false;
		}

		std::string TextureAbsolutePath(const EditorState& state, const std::string& relative) {
			return (std::filesystem::path(TexturesDir(state)) / relative)
				.lexically_normal().make_preferred().string();
		}

		std::string TextureRelativeName(const EditorState& state, const std::string& absolute) {
			if (absolute.empty()) {
				return {};
			}
			const std::filesystem::path root = std::filesystem::path(TexturesDir(state)).lexically_normal();
			std::filesystem::path rel = std::filesystem::path(absolute).lexically_normal().lexically_relative(root);
			if (rel.empty() || *rel.begin() == "..") {
				return {};
			}
			return rel.make_preferred().string();
		}

		bool ImportTexture(EditorState& state, const std::string& source_path,
			const std::string& subfolder, std::string& out_path, std::string& error) {
			namespace fs = std::filesystem;
			const std::string root = TexturesDir(state);
			if (root.empty()) {
				error = "no project open - textures are imported into <project>/Assets/Textures";
				return false;
			}
			std::error_code ec;
			if (!fs::is_regular_file(source_path, ec)) {
				error = "file not found: " + source_path;
				return false;
			}
			if (!IsImageFile(fs::path(source_path))) {
				error = "not an image the engine loads (.dds .png .jpg .bmp .tif)";
				return false;
			}
			const fs::path sub(subfolder);
			if (sub.has_root_name() || sub.has_root_directory()) {
				error = "the subfolder must be relative to Assets/Textures";
				return false;
			}
			for (const fs::path& part : sub) {
				if (part == "..") {
					error = "the subfolder cannot leave Assets/Textures";
					return false;
				}
			}
			const fs::path dest = (fs::path(root) / sub / fs::path(source_path).filename()).lexically_normal();
			fs::create_directories(dest.parent_path(), ec);
			if (!fs::exists(dest, ec) || !fs::equivalent(source_path, dest, ec)) {
				fs::copy_file(source_path, dest, fs::copy_options::overwrite_existing, ec);
				if (ec) {
					error = "could not copy into " + dest.string() + ": " + ec.message();
					return false;
				}
			}
			RefreshTextureList();
			out_path = fs::path(dest).make_preferred().string();
			state.status_message = "Imported texture " + dest.filename().string() + " into Assets/Textures";
			return true;
		}

		bool CreateShaderFile(EditorState& state, const std::string& stage,
			const std::string& source_shader, const std::string& new_name,
			std::string& out_cso_name, std::string& out_hlsl_path, std::string& error) {
			//Trim incidental whitespace, and drop an extension if one was typed - both
			//files are named from the stem, and the picker only ever shows .cso names.
			std::string name = new_name;
			while (!name.empty() && std::isspace((unsigned char)name.back())) {
				name.pop_back();
			}
			size_t start = 0;
			while (start < name.size() && std::isspace((unsigned char)name[start])) {
				++start;
			}
			name = name.substr(start);
			if (!name.empty() && std::filesystem::path(name).has_extension()) {
				name = std::filesystem::path(name).stem().string();
			}
			if (name.empty()) {
				error = "shader name is empty";
				return false;
			}
			if (name.size() < stage.size() ||
				name.compare(name.size() - stage.size(), stage.size(), stage) != 0) {
				error = "name must end with \"" + stage + "\" - the picker relies on the "
					"engine's naming convention to tell shaders of different stages apart";
				return false;
			}
			const std::string profile = ProfileForStage(stage);
			if (profile.empty()) {
				error = "unknown shader stage: " + stage;
				return false;
			}
			const std::string cso_name = name + ".cso";
			std::error_code ec;
			const std::string shaders_dir = ShadersDir(state);
			if (std::filesystem::exists(cso_name, ec) ||
				(!shaders_dir.empty() && std::filesystem::exists(std::filesystem::path(shaders_dir) / cso_name, ec))) {
				error = "a shader named " + cso_name + " already exists";
				return false;
			}
			if (!shaders_dir.empty()) {
				RegisterProjectShaderFolder(state);
			}

			//Starting content: a genuine copy of the currently selected shader's own
			//source, so every stage - not only VS/PS - starts from something already
			//known to compile. Falls back to a minimal template for the two stages
			//simple enough to have one.
			Core::ISimpleShader* current = Core::ShaderFactory::Get()->Find(source_shader);
			const std::string current_source = (current != nullptr) ? current->GetSourcePath() : std::string();
			std::string content;
			if (!current_source.empty()) {
				std::ifstream in(current_source, std::ios::binary);
				if (!in) {
					error = "could not read " + current_source + " to copy from";
					return false;
				}
				content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			}
			else {
				content = MinimalShaderTemplate(stage);
				if (content.empty()) {
					error = "no source is known for " + source_shader + " to copy from, and " +
						stage + " has no default template - select a shader with a known "
						"source first (see Shaders/Reload Changed)";
					return false;
				}
			}

			//The copy lands in the open project's own folder when there is one - a
			//custom shader belongs with the project using it, not scattered into the
			//engine's tracked source tree (duplicating one of ITS shaders would
			//otherwise write a stray file straight into Engine/Engine/Core/Shaders).
			//Falls back to the source's own folder, then the current directory, if no
			//project is open.
			//That folder is <project>/Assets/Shaders: every authored asset lives under
			//Assets, in the folder for its kind (see ImportShader).
			std::string dest_dir = shaders_dir;
			if (dest_dir.empty() || !std::filesystem::is_directory(dest_dir, ec)) {
				dest_dir = current_source.empty() ? "." : std::filesystem::path(current_source).parent_path().string();
			}
			const std::string hlsl_path = (std::filesystem::path(dest_dir) / (name + ".hlsl")).string();

			//No BOM: fxc rejects one in an #included file and silently keeps the stale
			//.cso for a top-level one (see CLAUDE.md). std::ofstream in binary mode
			//never adds one, unlike PowerShell's Set-Content -Encoding utf8.
			{
				std::ofstream out(hlsl_path, std::ios::binary | std::ios::trunc);
				if (!out) {
					error = "could not write " + hlsl_path;
					return false;
				}
				out << content;
			}

			//The source's own directory, so a copy relocated away from it still
			//resolves its quoted, parent-relative #includes ("../Common/...") - see
			//ShaderCompiler::CompileToFile.
			std::vector<std::string> extra_search_dirs;
			if (!current_source.empty()) {
				extra_search_dirs.push_back(std::filesystem::path(current_source).parent_path().string());
			}
			Core::ShaderCompiler::Result result;
			//The bytecode goes beside the source (Assets/Shaders is a registered binary
			//folder, so the name alone finds it); with no project it stays next to the exe.
			const std::string cso_path = shaders_dir.empty() ? cso_name
				: (std::filesystem::path(dest_dir) / cso_name).string();
			if (!Core::ShaderCompiler::Get()->CompileToFile(hlsl_path, profile, cso_path, result, extra_search_dirs)) {
				error = "compile failed: " + result.error;
				return false;
			}
			if (result.blob != nullptr) {
				result.blob->Release();
			}

			//Re-adding an already-registered folder is a rescan
			//(ShaderCompiler::AddSourceFolder), which is what makes the new file's
			//source resolve immediately - needed for the Edit button and hot reload
			//to find it without the user having to touch the source folder list.
			Core::ShaderCompiler::Get()->AddSourceFolder(dest_dir);
			Core::ShaderFactory::Get()->ForgetSourcePaths();
			RefreshShaderList();

			out_cso_name = cso_name;
			out_hlsl_path = hlsl_path;
			state.status_message = "Created " + cso_name + " from " + hlsl_path;
			return true;
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

			//The "New Shader" modal's state, shared the same way DrawCreateModal's
			//name_buf is: only one such popup is ever open at a time, whichever row's
			//"New..." button was last clicked, and its own PushID(label) scope keeps
			//nine simultaneous BeginPopupModal("New Shader") calls per frame from
			//colliding with each other.
			char new_shader_name[128] = "";
			std::string new_shader_stage;
			std::string new_shader_source;
			std::string new_shader_error;

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

			//Opens the native "Open File" dialog filtered to the image formats
			//Core::LoadTexture actually loads (.dds through CreateDDSTextureFromFile,
			//everything else through WIC - see Material.cpp), the same pattern
			//AssetBrowser::ImportModelWithDialog uses for models. `path` is left
			//untouched on Cancel.
			bool BrowseForTexture(std::string& path) {
				char file[MAX_PATH] = {};
				OPENFILENAMEA ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = nullptr;
				ofn.lpstrFilter = "Image files\0*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0"
								  "DDS textures\0*.dds\0"
								  "All files\0*.*\0";
				ofn.lpstrFile = file;
				ofn.nMaxFile = sizeof(file);
				ofn.lpstrTitle = "Select Texture";
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
				if (!GetOpenFileNameA(&ofn)) {
					return false;
				}
				path = file;
				return true;
			}

			//Same, for a shader: a .hlsl source or a compiled .cso.
			bool BrowseForShader(std::string& path) {
				char file[MAX_PATH] = {};
				OPENFILENAMEA ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = nullptr;
				ofn.lpstrFilter = "Shader files\0*.hlsl;*.cso\0All files\0*.*\0";
				ofn.lpstrFile = file;
				ofn.nMaxFile = sizeof(file);
				ofn.lpstrTitle = "Import Shader";
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
				if (!GetOpenFileNameA(&ofn)) {
					return false;
				}
				path = file;
				return true;
			}

			//A texture slot. Textures are *imported* into <project>/Assets/Textures
			//(subfolders allowed) and picked from there - a slot never points at a file
			//lying elsewhere on disk, so a level carries everything it draws with. The
			//"Import..." popup is where a file from outside comes in: it is copied into
			//the chosen subfolder and assigned to this slot in one step.
			//
			//In memory the path is still the absolute one MaterialData holds (see
			//MaterialTextures in Material.h); Save() writes it relative to the .mat's
			//texture root. A material authored before this rule may still name a file
			//outside Assets/Textures - it shows as "(external)" and Import... is
			//pre-filled with it, which is the one-click way to bring it in.
			bool DrawTextureRow(EditorState& state, const char* label, std::string& path) {
				static char import_source[MAX_PATH] = "";
				static char import_subfolder[128] = "";
				static std::string import_error;

				bool changed = false;
				ImGui::PushID(label);
				const std::string relative = MaterialOps::TextureRelativeName(state, path);
				std::string shown = path.empty() ? std::string("(none)")
					: (relative.empty() ? "(external) " + path : relative);

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
				if (ImGui::BeginCombo(label, shown.c_str())) {
					if (ImGui::Selectable("(none)", path.empty()) && !path.empty()) {
						path.clear();
						changed = true;
					}
					for (const std::string& option : MaterialOps::ListTextures(state)) {
						if (ImGui::Selectable(option.c_str(), option == relative) && option != relative) {
							path = MaterialOps::TextureAbsolutePath(state, option);
							changed = true;
						}
					}
					if (MaterialOps::ListTextures(state).empty()) {
						ImGui::TextDisabled("(nothing imported yet - use Import...)");
					}
					ImGui::EndCombo();
				}
				if (!path.empty() && relative.empty() && ImGui::IsItemHovered()) {
					ImGui::SetTooltip("This file is outside Assets/Textures, so the level would not\n"
						"carry it. Import... copies it into the project.");
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Import...")) {
					import_error.clear();
					snprintf(import_source, sizeof(import_source), "%s",
						(!path.empty() && relative.empty()) ? path.c_str() : "");
					import_subfolder[0] = '\0';
					ImGui::OpenPopup("Import Texture");
				}
				if (ImGui::BeginPopupModal("Import Texture", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::TextDisabled("Copies the file into <project>/Assets/Textures/<subfolder>.");
					ImGui::InputText("File", import_source, sizeof(import_source));
					ImGui::SameLine();
					if (ImGui::SmallButton("Browse...")) {
						std::string picked;
						if (BrowseForTexture(picked)) {
							snprintf(import_source, sizeof(import_source), "%s", picked.c_str());
						}
					}
					ImGui::InputText("Subfolder", import_subfolder, sizeof(import_subfolder));
					if (ImGui::BeginCombo("##existing_folders", "Existing folders")) {
						std::set<std::string> folders;
						for (const std::string& option : MaterialOps::ListTextures(state)) {
							const std::string dir = std::filesystem::path(option).parent_path().string();
							if (!dir.empty()) {
								folders.insert(dir);
							}
						}
						if (ImGui::Selectable("(Assets/Textures itself)")) {
							import_subfolder[0] = '\0';
						}
						for (const std::string& dir : folders) {
							if (ImGui::Selectable(dir.c_str())) {
								snprintf(import_subfolder, sizeof(import_subfolder), "%s", dir.c_str());
							}
						}
						ImGui::EndCombo();
					}
					if (!import_error.empty()) {
						ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", import_error.c_str());
					}
					if (ImGui::Button("Import")) {
						std::string imported;
						if (MaterialOps::ImportTexture(state, import_source, import_subfolder,
							imported, import_error)) {
							path = imported;
							changed = true;
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				return changed;
			}

			//Opens a file in Visual Studio Code via the `code` launcher on PATH.
			//ShellExecute rather than a blocking system() call, so a slow or missing
			//"code" does not stall the render thread.
			void OpenInVSCode(EditorState& state, const std::string& path) {
				const std::string args = "\"" + path + "\"";
				HINSTANCE result = ShellExecuteA(nullptr, "open", "code", args.c_str(), nullptr, SW_SHOWNORMAL);
				//ShellExecute returns a value > 32 on success; anything else is an error
				//code masquerading as a fake HINSTANCE (see its documentation).
				if ((INT_PTR)result <= 32) {
					state.status_message = "Could not open Visual Studio Code for " + path +
						" (is \"code\" on PATH?)";
				}
			}

			//Opens a shader's resolved .hlsl source, via the same lookup ShaderReload
			//compiles from (Core::ISimpleShader::GetSourcePath(), cached the first time
			//a reload or this button resolves it), so "the current one" always means
			//the file a reload would actually recompile, not just the .cso name shown
			//in the combo. A brand new shader (CreateShaderFile) is not resolvable this
			//way yet - ShaderFactory has not loaded its name at that point - so that
			//path calls OpenInVSCode directly on the path it already wrote.
			void EditShaderSource(EditorState& state, const std::string& shader_name) {
				Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(shader_name);
				const std::string source = (shader != nullptr) ? shader->GetSourcePath() : std::string();
				if (source.empty()) {
					state.status_message = "No .hlsl source found for " + shader_name;
					return;
				}
				OpenInVSCode(state, source);
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
			if (file.empty()) {
				//Authored inside an imported model: nothing above ever calls Touch for
				//it (there is no file to mark dirty), so any edit below is session-only
				//until it is moved into one of the level's own .mat files.
				ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
					"File: (none) - edits will not be saved");
				std::vector<std::string> files;
				for (const auto& entry : state.world->GetMaterialFiles()) {
					files.push_back(entry.first);
				}
				if (files.empty()) {
					ImGui::TextDisabled("This level declares no material files to move it into.");
				}
				else {
					//Keyed by material name (rather than one shared static, as the "New
					//Shader" popup state is) because this panel can be drawn twice in one
					//frame - once for the Materials panel's selection, once for the
					//Components panel's Material section - for two different materials.
					static std::map<std::string, int> assign_file_index;
					int& file_index = assign_file_index[name];
					file_index = (std::min)((std::max)(file_index, 0), (int)files.size() - 1);
					ImGui::PushID("assign_file");
					ImGui::SetNextItemWidth(160);
					if (ImGui::BeginCombo("##file", files[file_index].c_str())) {
						for (int i = 0; i < (int)files.size(); ++i) {
							if (ImGui::Selectable(files[i].c_str(), i == file_index)) {
								file_index = i;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Move to file")) {
						std::string move_error;
						if (!MaterialOps::AssignMaterialFile(state, name, files[file_index], move_error)) {
							state.status_message = move_error;
						}
						else {
							state.status_message = "Moved " + name + " to " + files[file_index];
						}
					}
					ImGui::PopID();
				}
			}
			else {
				ImGui::TextDisabled("File: %s%s", file.c_str(),
					state.dirty_material_files.count(file) != 0 ? "  (unsaved)" : "");
			}
			if (m->multi_material != nullptr) {
				//The stack's layers replace this material's own diffuse/normal/spec/ao/
				//height maps on the draw path (see MainRenderPS.hlsli); everything else -
				//emission, opacity, the flag checkboxes, the shader slots - still applies.
				//Said here rather than merely leaving the Textures section looking inert,
				//since "why doesn't my diffuse map do anything" is the natural question.
				ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
					"Wearing multi-material '%s' - its layers replace the maps below.",
					m->multi_material_name.c_str());
			}
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
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("How far the height map pushes the surface along its normal.\n"
					"Applies only while tessellation is on (mode and factor below) and a\n"
					"height map is set; 0 turns displacement off.");
			}
			track(ImGui::DragFloat("Tessellate", &m->tessellation_factor, 0.1f, 0.0f, 64.0f));
			{
				//The vertex shader tessellates only for a type above 0, so without this the
				//factor above is inert - a material's type used to be reachable only by
				//editing its .mat file.
				const std::vector<MaterialOps::TessMode>& modes = MaterialOps::TessellationModes();
				if (ImGui::BeginCombo("Tess mode", MaterialOps::TessellationModeName(m->tessellation_type))) {
					for (const MaterialOps::TessMode& mode : modes) {
						if (ImGui::Selectable(mode.name, mode.type == m->tessellation_type) &&
							mode.type != m->tessellation_type) {
							m->tessellation_type = mode.type;
							//Picked in a popup, so it never reports the activate/deactivate pair the
							//drag tracking above waits for: record it directly, against the snapshot
							//taken before any widget this frame wrote.
							MaterialOps::RecordEdit(state, name, frame_before);
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", mode.help);
						}
					}
					ImGui::EndCombo();
				}
			}
			track(ImGui::DragFloat("UV scale", &p.uv_scale, 0.01f, 0.01f, 100.0f));
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Multiplies the mesh's own UV before sampling the maps below -\n"
					"higher repeats the texture more often across the surface. Ordinary\n"
					"materials only: has no effect while World-aligned tiling is on\n"
					"(world_uv_scale governs tiling then) or while a multi-material is\n"
					"attached (each layer has its own uv_scale).");
			}
			track(ImGui::DragFloat("World tile size", &p.world_uv_scale, 0.01f, 0.01f, 1000.0f));

			//The flags the .mat file actually carries; the rest of props.flags is
			//derived from which texture maps are present (MaterialData::Init).
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
			//World tile size above is this flag's tile size in world units; ordinary
			//materials only (MainRenderPS.hlsli skips it when multi_texture_count > 0).
			flag_checkbox("World-aligned tiling", WORLD_UV_ENABLED_FLAG);

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
				ImGui::TextDisabled("Pick from Assets/Textures, or Import... a file into it.");
				if (ImGui::SmallButton("Rescan textures")) {
					MaterialOps::RefreshTextureList();
				}
				Core::MaterialTextures& t = m->texture_names;
				MaterialOps::MaterialSnapshot before_textures;
				MaterialOps::GetSnapshot(state, name, before_textures);
				bool texture_changed = false;
				texture_changed |= DrawTextureRow(state, "Diffuse map", t.diffuse_texname);
				texture_changed |= DrawTextureRow(state, "Normal map", t.normal_textname);
				texture_changed |= DrawTextureRow(state, "Height map", t.high_textname);
				texture_changed |= DrawTextureRow(state, "Specular map", t.spec_textname);
				texture_changed |= DrawTextureRow(state, "AO map", t.ao_textname);
				texture_changed |= DrawTextureRow(state, "ARM map", t.arm_textname);
				texture_changed |= DrawTextureRow(state, "Emission map", t.emission_textname);
				texture_changed |= DrawTextureRow(state, "Opacity map", t.opacity_textname);
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
					ImGui::SameLine();
					ImGui::BeginDisabled(value.empty());
					if (ImGui::SmallButton("Edit")) {
						EditShaderSource(state, value);
					}
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Open this shader's .hlsl source in Visual Studio\n"
							"Code - the same file Shaders/Reload Changed compiles from.");
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("New...")) {
						new_shader_stage = stage;
						new_shader_source = value;
						new_shader_error.clear();
						const std::string suggested = value.empty() ? (std::string("New") + stage) :
							(std::filesystem::path(value).stem().string() + "_Copy");
						snprintf(new_shader_name, sizeof(new_shader_name), "%s", suggested.c_str());
						ImGui::OpenPopup("New Shader");
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Create a new %s shader, starting as a copy of the one\n"
							"currently selected, and open it for editing.", stage);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Import...")) {
						std::string source;
						if (BrowseForShader(source)) {
							std::string imported, import_error;
							if (MaterialOps::ImportShader(state, source, imported, import_error)) {
								//The picker is stage-checked by name suffix, so an import of
								//another stage's file is refused rather than assigned here.
								const std::string stem = std::filesystem::path(imported).stem().string();
								if (stem.size() >= strlen(stage) &&
									stem.compare(stem.size() - strlen(stage), strlen(stage), stage) == 0) {
									value = imported;
									shader_changed = true;
								}
								else {
									state.status_message = "Imported " + imported + " - it is not a " +
										stage + " shader, so it was not assigned to this slot";
								}
							}
							else {
								state.status_message = "Import shader failed: " + import_error;
							}
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Copy a .hlsl (compiled on the way in) or .cso into\n"
							"<project>/Assets/Shaders and use it here.");
					}
					if (ImGui::BeginPopupModal("New Shader", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						ImGui::Text("New %s shader name (must end with \"%s\"):", stage, stage);
						ImGui::InputText("##new_shader_name", new_shader_name, sizeof(new_shader_name));
						ImGui::TextDisabled(new_shader_source.empty() ?
							"Starts from a minimal template." :
							"Starts as a copy of the current selection.");
						if (!new_shader_error.empty()) {
							ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", new_shader_error.c_str());
						}
						if (ImGui::Button("Create")) {
							std::string created_cso, created_hlsl;
							if (MaterialOps::CreateShaderFile(state, new_shader_stage, new_shader_source,
								new_shader_name, created_cso, created_hlsl, new_shader_error)) {
								value = created_cso;
								shader_changed = true;
								OpenInVSCode(state, created_hlsl);
								new_shader_name[0] = '\0';
								ImGui::CloseCurrentPopup();
							}
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel")) {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
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

			if (ImGui::CollapsingHeader("Multi-material")) {
				ImGui::TextDisabled("Attach a layer stack (see the Multi-Materials tab) to "
					"paint several materials over this one's surface.");
				const std::string current = m->multi_material_name;
				if (ImGui::BeginCombo("Stack", current.empty() ? "(none)" : current.c_str())) {
					if (ImGui::Selectable("(none)", current.empty())) {
						std::string set_error;
						if (!MultiMaterialOps::Assign(state, name, std::string(), set_error)) {
							state.status_message = "Detach failed: " + set_error;
						}
					}
					for (const std::string& mm_name : MultiMaterialOps::List(state)) {
						if (ImGui::Selectable(mm_name.c_str(), mm_name == current) &&
							mm_name != current) {
							std::string set_error;
							if (!MultiMaterialOps::Assign(state, name, mm_name, set_error)) {
								state.status_message = "Attach failed: " + set_error;
							}
						}
					}
					ImGui::EndCombo();
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

			if (ImGui::BeginTabBar("##material_panel_tabs")) {
				if (ImGui::BeginTabItem("Materials")) {
					DrawMaterialsTab(state);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Multi-Materials")) {
					MultiMaterialPanel::Draw(state);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Poly Haven", nullptr,
					PolyHaven::ConsumeShowRequest() ? ImGuiTabItemFlags_SetSelected : 0)) {
					PolyHaven::DrawTab(state);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::End();
		}

		void DrawMaterialsTab(EditorState& state) {
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
		}
	}
}
