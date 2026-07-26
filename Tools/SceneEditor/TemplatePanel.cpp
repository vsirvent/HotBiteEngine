#include "TemplatePanel.h"
#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "Inspector.h"
#include "MaterialPanel.h"

#include "imgui.h"
#include <World.h>
#include <ECS/ComponentRegistry.h>
#include <Components/Base.h>
#include <Components/Physics.h>

#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>

#pragma comment(lib, "comdlg32.lib")

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace TemplateOps {
		namespace {

			//Where authored templates live, relative to the level's assets path. The
			//level references a template by exactly this string ("Templates\foo.tpl"),
			//and World::Load resolves it against the same assets path, so the two are
			//kept in step by construction rather than by a filesystem relative-path
			//computation that would have to guess which of the two path conventions a
			//given level uses for "path".
			constexpr const char* TEMPLATE_SUBDIR = "Templates";
			constexpr const char* TEMPLATE_EXTENSION = ".tpl";

			//The stand-in mesh World creates on demand. It is engine bookkeeping, not an
			//authored asset, so it must not appear in the mesh picker - a template that
			//has not been pointed at a real mesh yet is already using it.
			bool IsInternalAsset(const std::string& name) {
				return name.rfind("__default_", 0) == 0;
			}

			TemplateAsset* FindAsset(EditorState& state, const std::string& name) {
				for (TemplateAsset& t : state.templates) {
					if (t.name == name) {
						return &t;
					}
				}
				return nullptr;
			}

			//Adds (or refreshes) the Asset Browser listing entry for an authored
			//template. Every path that registers one goes through here, so the panel,
			//the Asset Browser and the automation `list_templates` command can never
			//disagree about which templates exist.
			void RegisterAsset(EditorState& state, const std::string& name) {
				TemplateAsset* asset = FindAsset(state, name);
				if (asset == nullptr) {
					state.templates.push_back(TemplateAsset{});
					asset = &state.templates.back();
				}
				asset->name = name;
				asset->file_path = TemplateFilePath(state, name);
				asset->loaded = true;
				asset->authored = true;
			}

			void UnregisterAsset(EditorState& state, const std::string& name) {
				for (auto it = state.templates.begin(); it != state.templates.end(); ++it) {
					if (it->name == name) {
						state.templates.erase(it);
						return;
					}
				}
			}
		}

		bool IsMandatory(const std::string& component) {
			//Exactly what World::SpawnInstance reads off a template entity to clone it.
			return component == Base::NAME || component == Transform::NAME ||
				component == Mesh::NAME || component == Material::NAME ||
				component == Bounds::NAME;
		}

		std::string TemplateFilePath(const EditorState& state, const std::string& name) {
			if (state.world == nullptr) {
				return std::string();
			}
			return (fs::path(state.world->GetAssetsPath()) / TEMPLATE_SUBDIR /
				(name + TEMPLATE_EXTENSION)).string();
		}

		std::string TemplateReference(const std::string& name) {
			return std::string(TEMPLATE_SUBDIR) + "\\" + name + TEMPLATE_EXTENSION;
		}

		bool IsAuthored(const EditorState& state, const std::string& name) {
			return state.world != nullptr && state.world->IsAuthoredTemplate(name);
		}

		std::vector<std::string> ListAuthored(const EditorState& state) {
			std::vector<std::string> names;
			for (const TemplateAsset& t : state.templates) {
				if (t.authored) {
					names.push_back(t.name);
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		std::vector<std::string> ListPlaceable(const EditorState& state) {
			std::vector<std::string> names;
			for (const TemplateAsset& t : state.templates) {
				names.push_back(t.name);
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		std::vector<std::string> ListMeshes(const EditorState& state) {
			std::vector<std::string> names;
			if (state.world == nullptr) {
				return names;
			}
			for (const Core::MeshData& mesh : state.world->GetMeshes().GetData()) {
				if (!mesh.name.empty() && !IsInternalAsset(mesh.name)) {
					names.push_back(mesh.name);
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		std::vector<std::string> ListAnimationSets(const EditorState& state) {
			if (state.world == nullptr) {
				return {};
			}
			//Keyed by the stem of the FBX each set was loaded from, which is also the
			//name its animations carry unless the file was loaded with animation names
			//of its own - so these read as "troll_idle", "troll_walk", ...
			std::vector<std::string> names = state.world->GetSkeletons().Keys();
			std::sort(names.begin(), names.end());
			return names;
		}

		std::vector<std::string> ListTemplateAnimations(const EditorState& state,
			const std::string& name) {
			if (state.world == nullptr) {
				return {};
			}
			//Read off the template entity rather than off the mesh asset by name: the
			//entity is where the attachments actually landed, so this answers "what can
			//this template play right now" including sets attached a moment ago.
			Coordinator* tc = state.world->GetTemplatesCoordinator();
			Entity te = state.world->GetTemplateEntity(name);
			if (te == INVALID_ENTITY_ID || !tc->ContainsComponent<Mesh>(te)) {
				return {};
			}
			Core::MeshData* data = tc->GetComponent<Mesh>(te).GetData();
			if (data == nullptr) {
				return {};
			}
			return state.world->GetMeshAnimations(data->name);
		}

		nlohmann::json GetComponent(const EditorState& state, const std::string& name,
			const std::string& component) {
			if (state.world == nullptr) {
				return nlohmann::json::object();
			}
			const nlohmann::json* components = state.world->GetTemplateComponents(name);
			if (components == nullptr || !components->contains(component)) {
				return nlohmann::json::object();
			}
			return (*components)[component];
		}

		std::vector<std::string> ListComponents(const EditorState& state, const std::string& name) {
			std::vector<std::string> names;
			if (state.world == nullptr) {
				return names;
			}
			const nlohmann::json* components = state.world->GetTemplateComponents(name);
			if (components == nullptr) {
				return names;
			}
			//Registry order first, so the sections read the same way they do in the
			//Components panel...
			for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
				if (IsMandatory(desc.name) || components->contains(desc.name)) {
					names.push_back(desc.name);
				}
			}
			//...then anything the .tpl carries that this binary has no component for.
			//A template authored against a game that registers more components than the
			//Scene Editor does still round-trips them (SaveTemplateFile dumps the block
			//wholesale), so they are listed rather than silently invisible.
			for (const auto& [key, value] : components->items()) {
				if (ComponentRegistry::Instance().Find(key) == nullptr) {
					names.push_back(key);
				}
			}
			return names;
		}

		bool IsInline(const EditorState& state, const std::string& name) {
			return state.inline_templates.count(name) != 0;
		}

		bool GetSnapshot(EditorState& state, const std::string& name, TemplateSnapshot& out) {
			if (state.world == nullptr) {
				return false;
			}
			const nlohmann::json* components = state.world->GetTemplateComponents(name);
			out.exists = (components != nullptr);
			out.inline_in_level = IsInline(state, name);
			out.components = (components != nullptr) ? *components : nlohmann::json::object();
			return true;
		}

		bool ApplySnapshot(EditorState& state, const std::string& name,
			const TemplateSnapshot& snapshot, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			if (!snapshot.exists) {
				if (!state.world->IsAuthoredTemplate(name)) {
					//Already gone: applying the same removal twice is not an error, and
					//an undo/redo pair must be able to run either way round.
					return true;
				}
				const bool was_inline = IsInline(state, name);
				state.world->RemoveTemplate(name);
				UnregisterAsset(state, name);
				state.inline_templates.erase(name);
				if (state.selected_template == name) {
					state.selected_template.clear();
				}
				state.dirty_templates.erase(name);
				//A template stored inline has no file to unlink; the level dropping it
				//is the whole of its removal. One kept in a .tpl needs the file gone,
				//but only when that removal is actually saved.
				if (!was_inline) {
					state.removed_templates.insert(name);
				}
				return true;
			}
			if (!state.world->CreateTemplate(name, snapshot.components, error)) {
				return false;
			}
			RegisterAsset(state, name);
			if (snapshot.inline_in_level) {
				//Stored in the level: nothing to write to disk, so it never joins the
				//unsaved-templates set - saving the level is what persists it. Any .tpl
				//it used to live in is now a stale duplicate that the folder scan would
				//pick up again next session, so it is unlinked when the move is saved.
				state.inline_templates.insert(name);
				state.dirty_templates.erase(name);
				std::error_code ec;
				if (fs::exists(TemplateFilePath(state, name), ec)) {
					state.removed_templates.insert(name);
				}
			}
			else {
				state.inline_templates.erase(name);
				state.removed_templates.erase(name);
				state.dirty_templates.insert(name);
			}
			return true;
		}

		void RecordEdit(EditorState& state, const std::string& name,
			const TemplateSnapshot& before) {
			TemplateSnapshot after;
			if (!GetSnapshot(state, name, after)) {
				return;
			}
			if (before.exists == after.exists &&
				before.inline_in_level == after.inline_in_level &&
				before.components == after.components) {
				return;
			}
			std::string description = "edit template " + name;
			if (!before.exists) {
				description = "create template " + name;
			}
			else if (!after.exists) {
				description = "remove template " + name;
			}
			else if (before.inline_in_level != after.inline_in_level) {
				description = std::string("store template ") + name +
					(after.inline_in_level ? " in the level" : " in a file");
			}
			EditorHistory::Push({
				description,
				[name, before](EditorState& s) {
					std::string err;
					ApplySnapshot(s, name, before, err);
				},
				[name, after](EditorState& s) {
					std::string err;
					ApplySnapshot(s, name, after, err);
				} });
		}

		//Every create/duplicate/remove/edit funnels through here: snapshot, mutate,
		//record. Keeping it in one place is what guarantees the history rule in
		//EditorHistory.h holds for templates no matter which surface asked.
		static bool MutateTemplate(EditorState& state, const std::string& name,
			const nlohmann::json& components, bool remove, std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "no world";
				return false;
			}
			TemplateSnapshot target;
			target.exists = !remove;
			//Storage is orthogonal to what a mutation changes, so it carries over
			//rather than being reset to "in a file" by every edit.
			target.inline_in_level = before.inline_in_level;
			target.components = components;
			if (!ApplySnapshot(state, name, target, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			return true;
		}

		bool SetStorage(EditorState& state, const std::string& name, bool inline_in_level,
			std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before) || !before.exists) {
				error = "not an authored template: " + name;
				return false;
			}
			if (before.inline_in_level == inline_in_level) {
				return true;
			}
			TemplateSnapshot target = before;
			target.inline_in_level = inline_in_level;
			if (!ApplySnapshot(state, name, target, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			state.status_message = "Template " + name + " is now stored " +
				(inline_in_level ? "in the level" : "in " + TemplateReference(name));
			return true;
		}

		bool CreateTemplate(EditorState& state, const std::string& name, std::string& error) {
			if (name.empty()) {
				error = "template name is empty";
				return false;
			}
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			if (state.world->IsTemplateLoaded(name)) {
				error = "a template named '" + name + "' already exists";
				return false;
			}
			//An empty component block is not empty in effect: World::CreateTemplate
			//turns the absent Mesh and Material into the default cube and white
			//material, so a brand new template is immediately placeable and visible.
			if (!MutateTemplate(state, name, nlohmann::json::object(), false, error)) {
				return false;
			}
			state.selected_template = name;
			state.status_message = "Created template: " + name;
			return true;
		}

		bool CreateFromEntity(EditorState& state, const std::string& entity_name,
			const std::string& template_name, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			Coordinator* c = state.world->GetCoordinator();
			Entity e = (c != nullptr) ? c->GetEntityByName(entity_name) : INVALID_ENTITY_ID;
			if (e == INVALID_ENTITY_ID) {
				error = "entity not found: " + entity_name;
				return false;
			}
			if (template_name.empty()) {
				error = "template name is empty";
				return false;
			}
			if (state.world->IsTemplateLoaded(template_name)) {
				error = "a template named '" + template_name + "' already exists";
				return false;
			}
			if (!c->ContainsComponent<Mesh>(e) || !c->ContainsComponent<Transform>(e)) {
				//Without a mesh there is nothing for SpawnInstance to clone, so the
				//template would register but never place anything.
				error = entity_name + " has no Mesh/Transform, so it cannot become a template";
				return false;
			}

			nlohmann::json components = nlohmann::json::object();
			SerializeContext ctx = state.world->MakeSerializeContext();
			for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
				//Engine-managed components (Camera, Particles) are skipped: their policy
				//says they cannot be rebuilt from JSON, so carrying them would produce
				//instances the loader could not reconstruct.
				if (!desc.Addable() || !desc.has(c, e)) {
					continue;
				}
				try {
					components[desc.name] = desc.serialize(ctx, e);
				}
				catch (const std::exception&) {
					//A component that will not serialize is left out rather than
					//aborting the whole template.
				}
			}
			//A template is a *kind* of object, not a placement of one: it keeps the
			//entity's rotation and scale (those are part of how the object looks) but
			//starts at the origin, so every instance is positioned by where it is
			//placed rather than piling up on the source entity's spot.
			if (components.contains(Transform::NAME)) {
				components[Transform::NAME]["position"] = nlohmann::json{
					{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f} };
			}

			if (!MutateTemplate(state, template_name, components, false, error)) {
				return false;
			}
			state.selected_template = template_name;
			state.status_message = "Created template '" + template_name + "' from " + entity_name;
			return true;
		}

		bool DuplicateTemplate(EditorState& state, const std::string& source,
			const std::string& new_name, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			const nlohmann::json* components = state.world->GetTemplateComponents(source);
			if (components == nullptr) {
				error = "not an authored template: " + source;
				return false;
			}
			if (new_name.empty() || state.world->IsTemplateLoaded(new_name)) {
				error = "a template named '" + new_name + "' already exists";
				return false;
			}
			if (!MutateTemplate(state, new_name, *components, false, error)) {
				return false;
			}
			state.selected_template = new_name;
			state.status_message = "Duplicated template: " + source + " -> " + new_name;
			return true;
		}

		bool RemoveTemplate(EditorState& state, const std::string& name, std::string& error) {
			if (!IsAuthored(state, name)) {
				error = "not an authored template: " + name;
				return false;
			}
			const size_t placed = FindInstances(state, name).size();
			if (!MutateTemplate(state, name, nlohmann::json::object(), true, error)) {
				return false;
			}
			state.status_message = "Removed template: " + name;
			if (placed > 0) {
				state.status_message += " (" + std::to_string(placed) +
					" placed object(s) will not reload)";
			}
			return true;
		}

		bool ApplyComponent(EditorState& state, const std::string& name,
			const std::string& component, const nlohmann::json& value, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			const nlohmann::json* current = state.world->GetTemplateComponents(name);
			if (current == nullptr) {
				error = "not an authored template: " + name;
				return false;
			}
			nlohmann::json components = *current;
			components[component] = value;
			TemplateSnapshot target;
			target.exists = true;
			target.components = components;
			return ApplySnapshot(state, name, target, error);
		}

		bool SetComponent(EditorState& state, const std::string& name,
			const std::string& component, const nlohmann::json& value, std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "no world";
				return false;
			}
			if (!before.exists) {
				error = "not an authored template: " + name;
				return false;
			}
			if (!ApplyComponent(state, name, component, value, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			return true;
		}

		bool RemoveComponent(EditorState& state, const std::string& name,
			const std::string& component, std::string& error) {
			if (IsMandatory(component)) {
				error = component + " is what makes a template placeable and cannot be removed";
				return false;
			}
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before) || !before.exists) {
				error = "not an authored template: " + name;
				return false;
			}
			if (!before.components.contains(component)) {
				error = name + " has no " + component + " component";
				return false;
			}
			nlohmann::json components = before.components;
			components.erase(component);
			if (!MutateTemplate(state, name, components, false, error)) {
				return false;
			}
			state.status_message = "Removed " + component + " from template " + name;
			return true;
		}

		//The animation names one loaded animation set brings, walked the same way
		//Mesh::SetAnimation resolves a name.
		static std::vector<std::string> SkeletonAnimations(EditorState& state,
			const std::string& skeleton_name) {
			std::vector<std::string> names;
			std::shared_ptr<Core::Skeleton>* skl =
				state.world->GetSkeletons().Get(skeleton_name);
			if (skl == nullptr || *skl == nullptr) {
				return names;
			}
			for (const Core::JointCpuData& joint : (*skl)->CpuData()) {
				for (const Core::JointAnim& animation : joint.animations) {
					if (!animation.name.empty() && !animation.key_frames.empty()) {
						names.push_back(animation.name);
					}
				}
			}
			return names;
		}

		bool SetMesh(EditorState& state, const std::string& name,
			const std::string& mesh_name, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			block["name"] = mesh_name;
			//The animation belonged to the previous mesh. A name the new one cannot
			//offer is silently ignored by Mesh::SetAnimation, which would leave the
			//template claiming an animation it never plays - so it is dropped as part
			//of the same edit rather than as a second one the user would have to undo
			//separately. What the new mesh can offer is what it already carries plus
			//whatever the template's own animation sets will attach to it.
			const std::string animation = block.value("animation", std::string());
			if (!animation.empty()) {
				std::vector<std::string> available = state.world->GetMeshAnimations(mesh_name);
				if (block.contains("skeletons") && block["skeletons"].is_array()) {
					for (const auto& entry : block["skeletons"]) {
						if (!entry.is_string()) {
							continue;
						}
						for (const std::string& from_set :
							SkeletonAnimations(state, entry.get<std::string>())) {
							available.push_back(from_set);
						}
					}
				}
				if (std::find(available.begin(), available.end(), animation) == available.end()) {
					block.erase("animation");
					block.erase("animation_loop");
					block.erase("animation_speed");
				}
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool SetMaterial(EditorState& state, const std::string& name,
			const std::string& material_name, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Material::NAME);
			block["name"] = material_name;
			return SetComponent(state, name, Material::NAME, block, error);
		}

		bool SetAnimationSet(EditorState& state, const std::string& name,
			const std::string& skeleton_name, bool attached, std::string& error) {
			if (state.world == nullptr || state.world->GetSkeletons().Get(skeleton_name) == nullptr) {
				error = "unknown animation set: " + skeleton_name;
				return false;
			}
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			std::vector<std::string> sets;
			if (block.contains("skeletons") && block["skeletons"].is_array()) {
				for (const auto& entry : block["skeletons"]) {
					if (entry.is_string() && entry.get<std::string>() != skeleton_name) {
						sets.push_back(entry.get<std::string>());
					}
				}
			}
			if (attached) {
				sets.push_back(skeleton_name);
			}
			else if (block.value("animation", std::string()).rfind(skeleton_name, 0) == 0) {
				//The chosen animation came from the set being detached; leaving it
				//named would have the template claim an animation it cannot play.
				block.erase("animation");
				block.erase("animation_loop");
				block.erase("animation_speed");
			}
			if (sets.empty()) {
				block.erase("skeletons");
			}
			else {
				block["skeletons"] = sets;
			}
			//Detaching only stops the template *declaring* the set. The set itself
			//stays on the shared MeshData for this session, because other entities may
			//be playing it; the declaration is what a reload rebuilds from.
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool SetAnimation(EditorState& state, const std::string& name,
			const std::string& animation, bool loop, float speed, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			if (animation.empty()) {
				block.erase("animation");
				block.erase("animation_loop");
				block.erase("animation_speed");
			}
			else {
				block["animation"] = animation;
				block["animation_loop"] = loop;
				block["animation_speed"] = speed;
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool PlaceTemplate(EditorState& state, const std::string& name, PlacementMode mode,
			std::string& error) {
			return AssetBrowser::PlaceTemplate(state, name, mode, error);
		}

		bool ImportTemplate(EditorState& state, const std::string& tpl_path, std::string& error) {
			if (state.world == nullptr || state.world->GetAssetsPath().empty()) {
				error = "no level open";
				return false;
			}
			std::error_code ec;
			if (!fs::exists(tpl_path, ec)) {
				error = "file not found: " + tpl_path;
				return false;
			}
			//The name in the file wins over the filename, so importing a template does
			//not silently rename it - and so the collision below is checked against the
			//name it will actually register under.
			std::string name;
			nlohmann::json components;
			if (!state.world->ReadTemplateFile(tpl_path, false, name, components, error)) {
				return false;
			}
			if (state.world->IsTemplateLoaded(name)) {
				error = "a template named '" + name + "' already exists in this project";
				return false;
			}

			fs::path dest = TemplateFilePath(state, name);
			fs::create_directories(dest.parent_path(), ec);
			//Copying is skipped when the source already *is* the project's copy, which
			//is what happens when a template folder is browsed to in place.
			if (!fs::exists(dest, ec) || !fs::equivalent(fs::path(tpl_path), dest, ec)) {
				fs::copy_file(tpl_path, dest, fs::copy_options::overwrite_existing, ec);
				if (ec) {
					error = "could not copy into " + dest.string() + ": " + ec.message();
					return false;
				}
			}

			if (!MutateTemplate(state, name, components, false, error)) {
				return false;
			}
			//It came from a file and its file is already written; only the level's
			//reference to it is outstanding.
			state.dirty_templates.erase(name);
			state.selected_template = name;
			state.status_message = "Imported template: " + name;
			return true;
		}

		void ImportTemplateWithDialog(EditorState& state) {
			char file[MAX_PATH] = {};
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			//ImGui owns no native HWND here; nullptr is a valid dialog owner.
			ofn.hwndOwner = nullptr;
			ofn.lpstrFilter = "Template files\0*.tpl\0All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Import Template";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (!GetOpenFileNameA(&ofn)) {
				return;
			}
			std::string error;
			if (!ImportTemplate(state, file, error)) {
				state.status_message = "Import failed: " + error;
			}
		}

		std::vector<std::string> FindInstances(const EditorState& state, const std::string& name) {
			std::vector<std::string> names;
			for (const PlacedInstance& inst : state.placed_instances) {
				if (inst.template_name == name) {
					names.push_back(inst.name);
				}
			}
			return names;
		}

		bool HasUnsavedTemplates(const EditorState& state) {
			return !state.dirty_templates.empty() || !state.removed_templates.empty();
		}

		bool SaveTemplates(EditorState& state, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			int written = 0;
			for (const std::string& name : state.dirty_templates) {
				if (state.removed_templates.count(name) != 0) {
					continue; //about to be unlinked; writing it first would be pointless
				}
				if (!state.world->IsAuthoredTemplate(name) || IsInline(state, name)) {
					//An inline template's storage is the level; it has no file of its
					//own to write. (It should never be marked dirty either - this is
					//belt and braces.)
					continue;
				}
				if (!state.world->SaveTemplateFile(name, TemplateFilePath(state, name), error)) {
					return false;
				}
				++written;
			}
			//Only now is a removal permanent. Deferring the unlink to save time is what
			//lets Ctrl+Z undo a removal without having to restore a file from anywhere.
			int removed = 0;
			for (const std::string& name : state.removed_templates) {
				std::error_code ec;
				if (fs::remove(TemplateFilePath(state, name), ec)) {
					++removed;
				}
			}
			state.dirty_templates.clear();
			state.removed_templates.clear();
			state.status_message = "Saved " + std::to_string(written) + " template(s)" +
				(removed > 0 ? (", removed " + std::to_string(removed)) : "");
			return true;
		}

		void ScanTemplatesFolder(EditorState& state) {
			if (state.world == nullptr) {
				return;
			}
			fs::path dir = fs::path(state.world->GetAssetsPath()) / TEMPLATE_SUBDIR;
			std::error_code ec;
			if (fs::exists(dir, ec)) {
				for (const auto& entry : fs::directory_iterator(dir, ec)) {
					if (!entry.is_regular_file() || entry.path().extension() != TEMPLATE_EXTENSION) {
						continue;
					}
					const std::string name = entry.path().filename().replace_extension().string();
					//A template the level already listed is registered; re-loading it
					//would throw away edits made this session and re-read the file
					//behind them.
					if (!state.world->IsTemplateLoaded(name)) {
						std::string error;
						if (!state.world->LoadTemplateFile(entry.path().string(), false, error)) {
							printf("TemplateOps::ScanTemplatesFolder: %s\n", error.c_str());
							continue;
						}
					}
					else if (!state.world->IsAuthoredTemplate(name)) {
						//An .fbx already owns this name; the .tpl beside it is unusable.
						continue;
					}
					//Deliberately RegisterAsset and not ApplySnapshot: discovering a
					//template on disk is not an edit, so it must not mark the file dirty.
					RegisterAsset(state, name);
				}
			}

			//Templates the level's own "templates" array pulled in live only in the
			//World until now: the .fbx scan finds them only when they happen to sit in
			//Assets/Objects, so a level referencing an .fbx from anywhere else had a
			//template that could not be listed, selected or placed. List them all, so
			//"what this level can place" is one answer rather than two.
			for (const std::string& name : state.world->ListTemplates()) {
				if (FindAsset(state, name) != nullptr) {
					continue;
				}
				TemplateAsset asset;
				asset.name = name;
				asset.loaded = true;
				asset.authored = state.world->IsAuthoredTemplate(name);
				if (asset.authored) {
					asset.file_path = TemplateFilePath(state, name);
				}
				state.templates.push_back(asset);
			}
		}
	}

	namespace TemplatePanel {
		namespace {

			//The pre-edit snapshot of a drag in progress, and which template it belongs
			//to. One at a time is enough: ImGui only ever has one active widget.
			TemplateOps::TemplateSnapshot pending_before;
			std::string pending_template;
			bool pending_valid = false;

			//Euler angles for the Transform section's rotation drag, cached for the same
			//reason the Components panel caches them: repeatedly round-tripping a
			//quaternion through Euler jitters visibly near gimbal lock.
			std::string euler_template;
			float3 euler_cache{ 0.0f, 0.0f, 0.0f };

			//Raised by RequestTemplateFromSelection (the Edit menu) and consumed by the
			//toolbar, which is the only scope that may open the modal.
			bool from_selection_requested = false;

			void EndEdit(EditorState& state);

			void BeginEdit(EditorState& state, const std::string& name) {
				if (pending_valid && pending_template != name) {
					//A drag whose end was never seen (the panel switched templates
					//mid-edit): record what it did before starting a new one, rather
					//than losing that edit from the history.
					EndEdit(state);
				}
				if (pending_valid) {
					return;
				}
				TemplateOps::GetSnapshot(state, name, pending_before);
				pending_template = name;
				pending_valid = true;
			}

			void EndEdit(EditorState& state) {
				if (!pending_valid) {
					return;
				}
				TemplateOps::RecordEdit(state, pending_template, pending_before);
				pending_valid = false;
			}

			//Applies a component block mid-drag (no history) and records one action when
			//the drag ends. `changed`/`activated`/`finished` are the usual ImGui triple.
			void CommitBlock(EditorState& state, const std::string& name,
				const std::string& component, const nlohmann::json& block,
				bool changed, bool activated, bool finished) {
				if (activated) {
					BeginEdit(state, name);
				}
				if (changed) {
					std::string error;
					if (!TemplateOps::ApplyComponent(state, name, component, block, error)) {
						state.status_message = "Template edit failed: " + error;
					}
				}
				if (finished) {
					EndEdit(state);
				}
			}

			float3 JsonToFloat3(const nlohmann::json& j, const char* key, const float3& fallback) {
				if (!j.contains(key) || !j[key].is_object()) {
					return fallback;
				}
				return { j[key].value("x", fallback.x), j[key].value("y", fallback.y),
					j[key].value("z", fallback.z) };
			}

			float4 JsonToFloat4(const nlohmann::json& j, const char* key, const float4& fallback) {
				if (!j.contains(key) || !j[key].is_object()) {
					return fallback;
				}
				return { j[key].value("x", fallback.x), j[key].value("y", fallback.y),
					j[key].value("z", fallback.z), j[key].value("w", fallback.w) };
			}

			nlohmann::json FromFloat3(const float3& v) {
				return nlohmann::json{ {"x", v.x}, {"y", v.y}, {"z", v.z} };
			}

			nlohmann::json FromFloat4(const float4& v) {
				return nlohmann::json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
			}

			void DrawMeshSection(EditorState& state, const std::string& name) {
				nlohmann::json block = TemplateOps::GetComponent(state, name, Mesh::NAME);
				const std::string mesh_name = block.value("name", std::string());

				const std::vector<std::string> meshes = TemplateOps::ListMeshes(state);
				if (ImGui::BeginCombo("Mesh", mesh_name.empty() ? "(default cube)" : mesh_name.c_str())) {
					for (const std::string& option : meshes) {
						if (ImGui::Selectable(option.c_str(), option == mesh_name) &&
							option != mesh_name) {
							std::string error;
							if (!TemplateOps::SetMesh(state, name, option, error)) {
								state.status_message = "Set mesh failed: " + error;
							}
						}
					}
					if (meshes.empty()) {
						ImGui::TextDisabled("(this level has no mesh assets)");
					}
					ImGui::EndCombo();
				}

				//Which animation sets this template attaches to its mesh. A mesh can only
				//play animations belonging to a set attached to it, so this comes before
				//the animation picker - and is why that picker is empty on a level that
				//has not attached any.
				const std::vector<std::string> sets = TemplateOps::ListAnimationSets(state);
				if (!sets.empty() && ImGui::TreeNode("Animation sets")) {
					std::set<std::string> declared;
					if (block.contains("skeletons") && block["skeletons"].is_array()) {
						for (const auto& entry : block["skeletons"]) {
							if (entry.is_string()) {
								declared.insert(entry.get<std::string>());
							}
						}
					}
					for (const std::string& set : sets) {
						bool on = declared.count(set) != 0;
						if (ImGui::Checkbox(set.c_str(), &on)) {
							std::string error;
							if (!TemplateOps::SetAnimationSet(state, name, set, on, error)) {
								state.status_message = "Animation set failed: " + error;
							}
						}
					}
					ImGui::TextDisabled("Attaching a set makes its animations available on\n"
						"this mesh - and on every entity sharing the mesh, which\n"
						"is how the engine has always attached them.");
					ImGui::TreePop();
				}

				//Animations come from the sets attached to the chosen mesh, so a mesh
				//with none simply offers nothing rather than a free-text field that
				//would silently never match.
				const std::vector<std::string> animations =
					TemplateOps::ListTemplateAnimations(state, name);
				const std::string animation = block.value("animation", std::string());
				bool loop = block.value("animation_loop", true);
				float speed = block.value("animation_speed", 1.0f);
				ImGui::BeginDisabled(animations.empty());
				if (ImGui::BeginCombo("Animation", animation.empty() ? "(none)" : animation.c_str())) {
					if (ImGui::Selectable("(none)", animation.empty()) && !animation.empty()) {
						std::string error;
						TemplateOps::SetAnimation(state, name, "", loop, speed, error);
					}
					for (const std::string& option : animations) {
						if (ImGui::Selectable(option.c_str(), option == animation) &&
							option != animation) {
							std::string error;
							if (!TemplateOps::SetAnimation(state, name, option, loop, speed, error)) {
								state.status_message = "Set animation failed: " + error;
							}
						}
					}
					ImGui::EndCombo();
				}
				ImGui::EndDisabled();
				if (animations.empty()) {
					ImGui::TextDisabled(sets.empty()
						? "(this level loaded no animation sets)"
						: "(attach an animation set above to choose an animation)");
				}

				if (!animation.empty()) {
					if (ImGui::Checkbox("Loop", &loop)) {
						std::string error;
						TemplateOps::SetAnimation(state, name, animation, loop, speed, error);
					}
					bool changed = ImGui::DragFloat("Speed", &speed, 0.01f, 0.0f, 10.0f);
					bool activated = ImGui::IsItemActivated();
					bool finished = ImGui::IsItemDeactivatedAfterEdit();
					nlohmann::json edited = block;
					edited["animation_speed"] = speed;
					CommitBlock(state, name, Mesh::NAME, edited, changed, activated, finished);
				}
			}

			void DrawMaterialSection(EditorState& state, const std::string& name) {
				nlohmann::json block = TemplateOps::GetComponent(state, name, Material::NAME);
				const std::string material_name = block.value("name", std::string());
				const std::vector<std::string> materials = MaterialOps::ListMaterials(state);
				if (ImGui::BeginCombo("Material",
					material_name.empty() ? "(default white)" : material_name.c_str())) {
					for (const std::string& option : materials) {
						if (ImGui::Selectable(option.c_str(), option == material_name) &&
							option != material_name) {
							std::string error;
							if (!TemplateOps::SetMaterial(state, name, option, error)) {
								state.status_message = "Set material failed: " + error;
							}
						}
					}
					if (materials.empty()) {
						ImGui::TextDisabled("(this level has no materials)");
					}
					ImGui::EndCombo();
				}
				ImGui::TextDisabled("Edit the material itself in the Materials panel;\n"
					"a template only chooses which one to use.");
			}

			void DrawTransformSection(EditorState& state, const std::string& name) {
				nlohmann::json block = TemplateOps::GetComponent(state, name, Transform::NAME);
				float3 position = JsonToFloat3(block, "position", { 0.0f, 0.0f, 0.0f });
				float3 scale = JsonToFloat3(block, "scale", { 1.0f, 1.0f, 1.0f });
				float4 rotation = JsonToFloat4(block, "rotation", { 0.0f, 0.0f, 0.0f, 1.0f });

				bool changed = false;
				bool activated = false;
				bool finished = false;
				auto track = [&](bool widget_changed) {
					changed |= widget_changed;
					activated |= ImGui::IsItemActivated();
					finished |= ImGui::IsItemDeactivatedAfterEdit();
				};
				track(ImGui::DragFloat3("Offset", &position.x, 0.05f));
				track(ImGui::DragFloat3("Scale", &scale.x, 0.01f));

				if (euler_template != name) {
					euler_cache = Inspector::QuaternionToEulerDegrees(rotation);
					euler_template = name;
				}
				const bool rotated = ImGui::DragFloat3("Rotation (deg)", &euler_cache.x, 0.5f);
				track(rotated);
				//Reseeded from the stored quaternion on every frame the drag is neither
				//active nor being changed, so it follows an undo or a template switch
				//without ever fighting an edit in progress. Only a real rotation edit
				//writes the quaternion back, so a pure position drag cannot accumulate
				//quaternion<->Euler round-trip error into the rotation.
				if (rotated) {
					rotation = float3_to_quaternion(euler_cache);
				}
				else if (!ImGui::IsItemActive()) {
					euler_cache = Inspector::QuaternionToEulerDegrees(rotation);
				}
				nlohmann::json edited = block;
				edited["position"] = FromFloat3(position);
				edited["scale"] = FromFloat3(scale);
				edited["rotation"] = FromFloat4(rotation);
				CommitBlock(state, name, Transform::NAME, edited, changed, activated, finished);

				ImGui::TextDisabled("The template's own offset from wherever an instance\n"
					"is placed. Usually zero; scale and rotation are part of\n"
					"how the object looks and do belong here.");
			}

			void DrawBoundsSection(EditorState& state, const std::string& name) {
				const nlohmann::json* components =
					state.world->GetTemplateComponents(name);
				const bool authored = components != nullptr && components->contains(Bounds::NAME);
				if (!authored) {
					//No block means "measure the mesh", which World::CreateTemplate does
					//on every redefinition - so show what it measured rather than an
					//empty section.
					Coordinator* tc = state.world->GetTemplatesCoordinator();
					Entity te = state.world->GetTemplateEntity(name);
					if (te != INVALID_ENTITY_ID && tc->ContainsComponent<Bounds>(te)) {
						const Bounds& b = tc->GetConstComponent<Bounds>(te);
						ImGui::Text("Center  %.2f %.2f %.2f", b.local_box.Center.x,
							b.local_box.Center.y, b.local_box.Center.z);
						ImGui::Text("Extents %.2f %.2f %.2f", b.local_box.Extents.x,
							b.local_box.Extents.y, b.local_box.Extents.z);
					}
					ImGui::TextDisabled("Measured from the mesh, and re-measured whenever\n"
						"the mesh changes.");
					if (ImGui::Button("Override...")) {
						Coordinator* tc2 = state.world->GetTemplatesCoordinator();
						Entity te2 = state.world->GetTemplateEntity(name);
						nlohmann::json block;
						if (te2 != INVALID_ENTITY_ID && tc2->ContainsComponent<Bounds>(te2)) {
							const box& local = tc2->GetConstComponent<Bounds>(te2).local_box;
							block["center"] = nlohmann::json{ {"x", local.Center.x},
								{"y", local.Center.y}, {"z", local.Center.z} };
							block["extents"] = nlohmann::json{ {"x", local.Extents.x},
								{"y", local.Extents.y}, {"z", local.Extents.z} };
						}
						std::string error;
						if (!TemplateOps::SetComponent(state, name, Bounds::NAME, block, error)) {
							state.status_message = "Override bounds failed: " + error;
						}
					}
					return;
				}

				nlohmann::json block = (*components)[Bounds::NAME];
				float3 center = JsonToFloat3(block, "center", { 0.0f, 0.0f, 0.0f });
				float3 extents = JsonToFloat3(block, "extents", { 0.5f, 0.5f, 0.5f });
				bool changed = false;
				bool activated = false;
				bool finished = false;
				auto track = [&](bool widget_changed) {
					changed |= widget_changed;
					activated |= ImGui::IsItemActivated();
					finished |= ImGui::IsItemDeactivatedAfterEdit();
				};
				track(ImGui::DragFloat3("Center", &center.x, 0.05f));
				track(ImGui::DragFloat3("Extents", &extents.x, 0.05f));
				nlohmann::json edited = block;
				edited["center"] = FromFloat3(center);
				edited["extents"] = FromFloat3(extents);
				CommitBlock(state, name, Bounds::NAME, edited, changed, activated, finished);

				//Dropping the block is the only way back to automatic measurement, so it
				//gets its own button rather than the section's remove affordance (Bounds
				//is mandatory on a template and so has none).
				if (ImGui::Button("Measure from mesh")) {
					nlohmann::json components_without = *components;
					components_without.erase(Bounds::NAME);
					std::string error;
					TemplateOps::TemplateSnapshot before;
					TemplateOps::GetSnapshot(state, name, before);
					TemplateOps::TemplateSnapshot target;
					target.exists = true;
					target.components = components_without;
					if (TemplateOps::ApplySnapshot(state, name, target, error)) {
						TemplateOps::RecordEdit(state, name, before);
					}
					else {
						state.status_message = "Measure bounds failed: " + error;
					}
				}
			}

			void DrawPhysicsSection(EditorState& state, const std::string& name) {
				nlohmann::json block = TemplateOps::GetComponent(state, name, Physics::NAME);
				static const char* TYPES[] = { "STATIC", "KINEMATIC", "DYNAMIC" };
				static const char* SHAPES[] = { "NONE", "CAPSULE", "BOX", "SPHERE" };

				auto enum_combo = [&](const char* label, const char* key,
					const char* const* options, int count, const char* fallback) {
						std::string current = block.value(key, std::string(fallback));
						if (!ImGui::BeginCombo(label, current.c_str())) {
							return;
						}
						for (int i = 0; i < count; ++i) {
							if (ImGui::Selectable(options[i], current == options[i]) &&
								current != options[i]) {
								nlohmann::json edited = block;
								edited[key] = options[i];
								std::string error;
								if (!TemplateOps::SetComponent(state, name, Physics::NAME, edited, error)) {
									state.status_message = "Set physics failed: " + error;
								}
							}
						}
						ImGui::EndCombo();
					};
				enum_combo("Body", "type", TYPES, 3, "STATIC");
				enum_combo("Shape", "shape", SHAPES, 4, "BOX");

				//Negative means "engine default"; the drags start there and a value is
				//only written once one is actually set, matching Physics::ToJson.
				float bounce = block.value("bounce", -1.0f);
				float friction = block.value("friction", -1.0f);
				float air_friction = block.value("air_friction", -1.0f);
				bool changed = false;
				bool activated = false;
				bool finished = false;
				auto track = [&](bool widget_changed) {
					changed |= widget_changed;
					activated |= ImGui::IsItemActivated();
					finished |= ImGui::IsItemDeactivatedAfterEdit();
				};
				track(ImGui::DragFloat("Bounce", &bounce, 0.01f, -1.0f, 1.0f, "%.2f"));
				track(ImGui::DragFloat("Friction", &friction, 0.01f, -1.0f, 1.0f, "%.2f"));
				track(ImGui::DragFloat("Air friction", &air_friction, 0.01f, -1.0f, 1.0f, "%.2f"));
				nlohmann::json edited = block;
				auto write_or_erase = [&](const char* key, float value) {
					if (value < 0.0f) {
						edited.erase(key);
					}
					else {
						edited[key] = value;
					}
				};
				write_or_erase("bounce", bounce);
				write_or_erase("friction", friction);
				write_or_erase("air_friction", air_friction);
				CommitBlock(state, name, Physics::NAME, edited, changed, activated, finished);
				ImGui::TextDisabled("-1 leaves the engine default alone.");
			}

			//One collapsing section per component of the template, with the same remove
			//affordance the Components panel uses (the header's own close button - see
			//the ImGui note in Inspector::DrawComponentSection for why a SameLine button
			//does not work over a header).
			void DrawComponentSection(EditorState& state, const std::string& name,
				const std::string& component) {
				ImGui::PushID(component.c_str());
				const ComponentDesc* desc = ComponentRegistry::Instance().Find(component);
				const bool removable = !TemplateOps::IsMandatory(component);

				bool visible = true;
				const bool open = ImGui::CollapsingHeader(component.c_str(),
					removable ? &visible : nullptr, ImGuiTreeNodeFlags_DefaultOpen);
				if (removable && !visible) {
					std::string error;
					if (!TemplateOps::RemoveComponent(state, name, component, error)) {
						state.status_message = "Remove failed: " + error;
					}
					ImGui::PopID();
					return;
				}
				if (!open) {
					ImGui::PopID();
					return;
				}
				//Its own ID scope, distinct from the header's: a widget labelled like its
				//component ("Material" inside "Material") otherwise collides with the
				//header and never activates.
				ImGui::PushID("body");
				if (component == Mesh::NAME) {
					DrawMeshSection(state, name);
				}
				else if (component == Material::NAME) {
					DrawMaterialSection(state, name);
				}
				else if (component == Transform::NAME) {
					DrawTransformSection(state, name);
				}
				else if (component == Bounds::NAME) {
					DrawBoundsSection(state, name);
				}
				else if (component == Physics::NAME) {
					DrawPhysicsSection(state, name);
				}
				else if (desc == nullptr) {
					//A block from a .tpl this binary has no component for. Read-only for
					//the same reason the Components panel shows opaque blocks read-only:
					//with no type behind it there is nothing to tell a meaningful edit
					//from a corrupting one. It is saved back byte for byte.
					ImGui::TextDisabled("(defined by the game, not by the editor)");
					ImGui::TextUnformatted(
						TemplateOps::GetComponent(state, name, component).dump(2).c_str());
				}
				else {
					//Everything else - Base, Player, lights, a game's own components -
					//through the generic grid over its serialized shape.
					nlohmann::json block = TemplateOps::GetComponent(state, name, component);
					if (block.empty()) {
						//Never edited: seed the grid from the template entity when it
						//carries the component, so the fields show real values instead of
						//an empty section.
						Coordinator* tc = state.world->GetTemplatesCoordinator();
						Entity te = state.world->GetTemplateEntity(name);
						if (te != INVALID_ENTITY_ID && tc != nullptr && desc->has(tc, te)) {
							SerializeContext ctx;
							ctx.world = state.world;
							ctx.coordinator = tc;
							try {
								block = desc->serialize(ctx, te);
							}
							catch (const std::exception&) {
							}
						}
					}
					bool finished = false;
					const bool changed = Inspector::DrawJsonGrid(block, &finished);
					//The grid has no single "activated" moment (it is a row of
					//independent widgets), so the first frame that changes anything
					//opens the edit; BeginEdit is idempotent for the rest of the drag.
					CommitBlock(state, name, component, block, changed, changed, finished);
				}
				ImGui::PopID();
				ImGui::PopID();
			}

			void DrawAddComponent(EditorState& state, const std::string& name) {
				if (ImGui::Button("Add Component")) {
					ImGui::OpenPopup("add_template_component");
				}
				if (!ImGui::BeginPopup("add_template_component")) {
					return;
				}
				const nlohmann::json* components = state.world->GetTemplateComponents(name);
				bool any = false;
				for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
					if (!desc.Addable() || TemplateOps::IsMandatory(desc.name) ||
						(components != nullptr && components->contains(desc.name))) {
						continue;
					}
					any = true;
					if (ImGui::MenuItem(desc.name.c_str())) {
						//An empty block is the component as constructed: every FromJson
						//treats a missing key as "leave alone".
						std::string error;
						if (!TemplateOps::SetComponent(state, name, desc.name,
							nlohmann::json::object(), error)) {
							state.status_message = "Add failed: " + error;
						}
					}
				}
				if (!any) {
					ImGui::TextDisabled("(nothing left to add)");
				}
				ImGui::EndPopup();
			}

			//A name field shared by the New / Duplicate / From Selection modals. Returns
			//true when the user confirmed with a non-empty name.
			bool DrawNameModal(const char* title, const char* prompt, char* buffer,
				size_t buffer_size) {
				bool confirmed = false;
				const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::TextUnformatted(prompt);
					ImGui::SetNextItemWidth(320.0f);
					const bool entered = ImGui::InputText("##name", buffer, buffer_size,
						ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::BeginDisabled(buffer[0] == '\0');
					const bool clicked = ImGui::Button("Create", ImVec2(90.0f, 0.0f));
					ImGui::EndDisabled();
					ImGui::SameLine();
					const bool cancel = ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) ||
						ImGui::IsKeyPressed(ImGuiKey_Escape, false);
					if ((entered || clicked) && buffer[0] != '\0') {
						confirmed = true;
						ImGui::CloseCurrentPopup();
					}
					else if (cancel) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				return confirmed;
			}

			//A name not already taken by a template, derived from `base`.
			std::string UniqueName(const EditorState& state, const std::string& base) {
				std::string candidate = base;
				int suffix = 2;
				while (state.world->IsTemplateLoaded(candidate)) {
					candidate = base + std::to_string(suffix++);
				}
				return candidate;
			}

			void DrawToolbar(EditorState& state) {
				static char new_name[128] = "";
				static char from_entity_name[128] = "";
				static char duplicate_name[128] = "";

				if (ImGui::Button("New...")) {
					strncpy_s(new_name, UniqueName(state, "template").c_str(), sizeof(new_name) - 1);
					ImGui::OpenPopup("New Template");
				}
				ImGui::SameLine();
				if (ImGui::Button("Import...")) {
					TemplateOps::ImportTemplateWithDialog(state);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Bring a .tpl authored elsewhere into this project");
				}

				//"From Selection" is the fast path: take the entity that is already set
				//up in the scene and turn it into something placeable.
				Coordinator* c = state.world->GetCoordinator();
				const bool has_entity = c != nullptr &&
					state.selected_entity != INVALID_ENTITY_ID &&
					c->ContainsComponent<Base>(state.selected_entity);
				const std::string entity_name = has_entity
					? c->GetConstComponent<Base>(state.selected_entity).name : std::string();
				ImGui::SameLine();
				ImGui::BeginDisabled(!has_entity);
				bool from_selection = ImGui::Button("From Selection");
				ImGui::EndDisabled();
				//The Edit menu asks for the same prompt; consumed here because this is
				//the scope that owns the modal.
				if (from_selection_requested) {
					from_selection_requested = false;
					from_selection = has_entity;
					if (!has_entity) {
						state.status_message = "No entity selected to make a template from.";
					}
				}
				if (from_selection) {
					strncpy_s(from_entity_name, UniqueName(state, entity_name + "_template").c_str(),
						sizeof(from_entity_name) - 1);
					ImGui::OpenPopup("Template From Entity");
				}
				if (has_entity && ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Make a template out of %s", entity_name.c_str());
				}

				const bool authored_selected =
					TemplateOps::IsAuthored(state, state.selected_template);
				ImGui::SameLine();
				ImGui::BeginDisabled(!authored_selected);
				if (ImGui::Button("Duplicate")) {
					strncpy_s(duplicate_name,
						UniqueName(state, state.selected_template + "_copy").c_str(),
						sizeof(duplicate_name) - 1);
					ImGui::OpenPopup("Duplicate Template");
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove")) {
					ImGui::OpenPopup("Remove Template");
				}
				ImGui::EndDisabled();

				ImGui::SameLine();
				ImGui::BeginDisabled(!TemplateOps::HasUnsavedTemplates(state));
				if (ImGui::Button("Save Templates")) {
					std::string error;
					if (!TemplateOps::SaveTemplates(state, error)) {
						state.status_message = "Save templates failed: " + error;
					}
				}
				ImGui::EndDisabled();
				if (TemplateOps::HasUnsavedTemplates(state)) {
					ImGui::SameLine();
					ImGui::TextDisabled("(%d unsaved)",
						(int)(state.dirty_templates.size() + state.removed_templates.size()));
				}

				std::string error;
				if (DrawNameModal("New Template", "Name for the new template:",
					new_name, sizeof(new_name))) {
					if (!TemplateOps::CreateTemplate(state, new_name, error)) {
						state.status_message = "Create failed: " + error;
					}
				}
				if (DrawNameModal("Template From Entity",
					"Name for the template made from the selected entity:",
					from_entity_name, sizeof(from_entity_name))) {
					if (!TemplateOps::CreateFromEntity(state, entity_name, from_entity_name, error)) {
						state.status_message = "Create failed: " + error;
					}
				}
				if (DrawNameModal("Duplicate Template", "Name for the copy:",
					duplicate_name, sizeof(duplicate_name))) {
					if (!TemplateOps::DuplicateTemplate(state, state.selected_template,
						duplicate_name, error)) {
						state.status_message = "Duplicate failed: " + error;
					}
				}

				const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal("Remove Template", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::Text("Remove template '%s'?", state.selected_template.c_str());
					const std::vector<std::string> placed =
						TemplateOps::FindInstances(state, state.selected_template);
					if (!placed.empty()) {
						ImGui::TextWrapped("%d object%s placed from it stay%s in the scene for "
							"now, but will not come back the next time this level is loaded - "
							"the record that recreates them names this template.",
							(int)placed.size(), placed.size() == 1 ? "" : "s",
							placed.size() == 1 ? "s" : "");
					}
					ImGui::TextUnformatted("Its .tpl file is deleted when templates are saved;\n"
						"until then this can be undone with Ctrl+Z.");
					ImGui::Separator();
					if (ImGui::Button("Remove", ImVec2(90.0f, 0.0f))) {
						if (!TemplateOps::RemoveTemplate(state, state.selected_template, error)) {
							state.status_message = "Remove failed: " + error;
						}
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			void DrawDetails(EditorState& state, const std::string& name) {
				ImGui::Text("%s", name.c_str());
				const bool authored = TemplateOps::IsAuthored(state, name);
				if (!authored) {
					ImGui::TextDisabled("Imported object (.fbx) - its components come from "
						"the file.");
				}

				if (ImGui::Button("Add to Scene")) {
					std::string error;
					if (!TemplateOps::PlaceTemplate(state, name, PlacementMode::ViewCenter, error)) {
						state.status_message = "Place failed: " + error;
					}
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Drop %s on whatever the middle of the view is "
						"looking at", name.c_str());
				}
				ImGui::SameLine();
				if (ImGui::Button("At Origin")) {
					std::string error;
					if (!TemplateOps::PlaceTemplate(state, name, PlacementMode::Origin, error)) {
						state.status_message = "Place failed: " + error;
					}
				}

				if (authored) {
					//Where the definition lives. Two radio buttons rather than a
					//checkbox, because "stored in the level" and "stored in a file" are
					//both first-class answers and the label has to say which file.
					const bool is_inline = TemplateOps::IsInline(state, name);
					ImGui::TextUnformatted("Stored in:");
					ImGui::SameLine();
					if (ImGui::RadioButton(TemplateOps::TemplateReference(name).c_str(), !is_inline) &&
						is_inline) {
						std::string error;
						if (!TemplateOps::SetStorage(state, name, false, error)) {
							state.status_message = "Storage change failed: " + error;
						}
					}
					ImGui::SameLine();
					if (ImGui::RadioButton("this level", is_inline) && !is_inline) {
						std::string error;
						if (!TemplateOps::SetStorage(state, name, true, error)) {
							state.status_message = "Storage change failed: " + error;
						}
					}
					if (!is_inline && state.dirty_templates.count(name) != 0) {
						ImGui::SameLine();
						ImGui::TextDisabled("(unsaved)");
					}
				}
				ImGui::Separator();

				if (!authored) {
					//An imported template's components are whatever the FBX produced;
					//they are shown so the two kinds of template read alike, but editing
					//them would have nowhere to be saved.
					Coordinator* tc = state.world->GetTemplatesCoordinator();
					const std::set<Entity>& parts = state.world->GetTemplateEntities(name);
					ImGui::Text("Parts: %d", (int)parts.size());
					for (Entity part : parts) {
						if (tc == nullptr || !tc->ContainsComponent<Base>(part)) {
							continue;
						}
						ImGui::BulletText("%s", tc->GetConstComponent<Base>(part).name.c_str());
					}
					ImGui::Spacing();
					ImGui::TextWrapped("To author components, use \"From Selection\" on an "
						"instance of this object: that captures its components into a "
						"template you can edit.");
					return;
				}

				ImGui::PushItemWidth(-140.0f);
				for (const std::string& component : TemplateOps::ListComponents(state, name)) {
					DrawComponentSection(state, name, component);
				}
				ImGui::PopItemWidth();
				ImGui::Spacing();
				DrawAddComponent(state, name);
				ImGui::Spacing();
				ImGui::TextDisabled("Templates are saved by File/Save Templates.\n"
					"Which templates a level uses is saved with the level.");
			}
		}

		void RequestTemplateFromSelection(EditorState& state) {
			from_selection_requested = true;
		}

		void Draw(EditorState& state) {
			if (state.world == nullptr) {
				return;
			}
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			const ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(
				ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.22f, vp->WorkPos.y + vp->WorkSize.y * 0.12f), cond);
			ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.48f, vp->WorkSize.y * 0.65f), cond);

			if (!ImGui::Begin(EditorLayout::TEMPLATES_WINDOW, &state.show_template_panel)) {
				ImGui::End();
				return;
			}
			//Same scan the Asset Browser runs, so opening either panel first shows the
			//same set of templates.
			AssetBrowser::EnsureTemplatesScanned(state);

			DrawToolbar(state);
			ImGui::Separator();

			const std::vector<std::string> names = TemplateOps::ListPlaceable(state);
			if (names.empty()) {
				ImGui::TextDisabled("This project has no templates yet.");
				ImGui::TextDisabled("Use \"New...\" to build one, \"From Selection\" to make one\n"
					"out of a scene entity, or File/Import Object... for an .fbx.");
				ImGui::End();
				return;
			}

			const float list_width = ImGui::GetContentRegionAvail().x * 0.32f;
			if (ImGui::BeginChild("##template_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_Border)) {
				for (const std::string& name : names) {
					const bool authored = TemplateOps::IsAuthored(state, name);
					std::string label = name;
					if (!authored) {
						label += "  (.fbx)";
					}
					else if (state.dirty_templates.count(name) != 0) {
						label += "  *";
					}
					if (ImGui::Selectable(label.c_str(), name == state.selected_template)) {
						state.selected_template = name;
					}
					//Double-click places it in view, which is the shortest path from "I
					//want this object" to having one where you are working.
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						std::string error;
						if (!TemplateOps::PlaceTemplate(state, name, PlacementMode::ViewCenter, error)) {
							state.status_message = "Place failed: " + error;
						}
					}
				}
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("##template_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
				if (state.selected_template.empty() ||
					!state.world->IsTemplateLoaded(state.selected_template)) {
					ImGui::TextDisabled("Select a template to edit it.");
				}
				else {
					DrawDetails(state, state.selected_template);
				}
			}
			ImGui::EndChild();
			ImGui::End();
		}
	}
}
