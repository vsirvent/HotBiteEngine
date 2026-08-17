#include "TemplatePanel.h"
#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "Inspector.h"
#include "MaterialPanel.h"
#include "ModelPreview.h"

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

		std::vector<std::string> ListSplatClouds(const EditorState& state) {
			std::vector<std::string> names;
			if (state.world == nullptr) {
				return names;
			}
			//The peer of ListMeshes over the world's splat cloud assets, and filtered the
			//same way: the stand-in cloud is what an unassigned component is already
			//drawing, not something to pick.
			for (const std::string& name : state.world->GetSplatClouds().Keys()) {
				if (!name.empty() && !IsInternalAsset(name)) {
					names.push_back(name);
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		std::vector<AvailableClip> ListAvailableClips(const EditorState& state) {
			std::vector<AvailableClip> clips;
			if (state.world == nullptr) {
				return clips;
			}
			//Walked model by model rather than set by set: which file an animation came
			//from is the only grouping that means anything to whoever is choosing one,
			//and an animation set is an implementation detail of that file.
			for (const ModelAsset& model : state.models) {
				const World::ModelAssets* assets = state.world->GetModelAssets(model.name);
				if (assets == nullptr) {
					continue;
				}
				for (const std::string& set : assets->animation_sets) {
					for (const std::string& clip : state.world->GetAnimationSetClips(set)) {
						clips.push_back({ model.name, clip });
					}
				}
			}
			std::sort(clips.begin(), clips.end(), [](const AvailableClip& a, const AvailableClip& b) {
				return (a.model != b.model) ? (a.model < b.model) : (a.clip < b.clip);
				});
			clips.erase(std::unique(clips.begin(), clips.end(),
				[](const AvailableClip& a, const AvailableClip& b) {
					return a.model == b.model && a.clip == b.clip;
				}), clips.end());
			return clips;
		}

		//Which model carries `clip`, for display. Goes through the world's set lookup
		//and then back to the model that brought that set in, so a clip whose file is
		//no longer listed reads as unresolved rather than as belonging to nothing in
		//particular.
		static std::string ModelOfClip(const EditorState& state, const std::string& clip) {
			if (state.world == nullptr) {
				return {};
			}
			const std::string set = state.world->FindAnimationSet(clip);
			if (set.empty()) {
				return {};
			}
			for (const ModelAsset& model : state.models) {
				const World::ModelAssets* assets = state.world->GetModelAssets(model.name);
				if (assets == nullptr) {
					continue;
				}
				if (std::find(assets->animation_sets.begin(), assets->animation_sets.end(), set) !=
					assets->animation_sets.end()) {
					return model.name;
				}
			}
			return set;
		}

		std::vector<TemplateClip> ListClips(const EditorState& state, const std::string& name) {
			std::vector<TemplateClip> result;
			const nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			if (!block.contains("clips") || !block["clips"].is_object()) {
				return result;
			}
			const std::string current = block.value("animation", std::string());
			for (auto it = block["clips"].begin(); it != block["clips"].end(); ++it) {
				if (!it.value().is_string()) {
					continue;
				}
				TemplateClip entry;
				entry.name = it.key();
				entry.clip = it.value();
				entry.model = ModelOfClip(state, entry.clip);
				entry.resolved = !entry.model.empty();
				entry.is_default = (entry.name == current);
				result.push_back(entry);
			}
			return result;
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
			const nlohmann::json* parts = state.world->GetTemplateParts(name);
			out.exists = (components != nullptr);
			out.inline_in_level = IsInline(state, name);
			out.components = (components != nullptr) ? *components : nlohmann::json::object();
			out.parts = (parts != nullptr) ? *parts : nlohmann::json::array();
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
			if (!state.world->CreateTemplate(name, snapshot.components, snapshot.parts, error)) {
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
				before.components == after.components &&
				before.parts == after.parts) {
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
			const nlohmann::json& components, bool remove, std::string& error,
			const nlohmann::json* parts = nullptr) {
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
			//So are the parts: editing a composed template's components must not quietly
			//decompose it, so they carry over unless the caller is the one editing them.
			target.parts = (parts != nullptr) ? *parts : before.parts;
			if (!ApplySnapshot(state, name, target, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			return true;
		}

		// == Parts =============================================================

		//The part entry named `part_name` inside a parts array, or null. One place,
		//because every edit below is "find it, change it, write the array back".
		static nlohmann::json* FindPartEntry(nlohmann::json& parts, const std::string& part_name) {
			if (!parts.is_array()) {
				return nullptr;
			}
			for (auto& part : parts) {
				if (part.is_object() && part.value("name", std::string()) == part_name) {
					return &part;
				}
			}
			return nullptr;
		}

		static nlohmann::json PartToJson(const TemplatePart& part) {
			nlohmann::json j;
			j["name"] = part.name;
			j["template"] = part.template_name;
			j["attach"] = part.attach;
			if (!part.bone.empty()) {
				j["bone"] = part.bone;
			}
			j["position"] = { {"x", part.position.x}, {"y", part.position.y}, {"z", part.position.z} };
			j["rotation"] = { {"x", part.rotation.x}, {"y", part.rotation.y},
							  {"z", part.rotation.z}, {"w", part.rotation.w} };
			j["scale"] = { {"x", part.scale.x}, {"y", part.scale.y}, {"z", part.scale.z} };
			if (part.components.is_object() && !part.components.empty()) {
				j["components"] = part.components;
			}
			return j;
		}

		static TemplatePart PartFromJson(const nlohmann::json& j) {
			TemplatePart part;
			part.name = j.value("name", std::string());
			part.template_name = j.value("template", std::string());
			part.attach = j.value("attach", true);
			part.bone = j.value("bone", std::string());
			if (j.contains("position")) {
				const auto& p = j["position"];
				part.position = { p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f) };
			}
			if (j.contains("rotation")) {
				const auto& r = j["rotation"];
				part.rotation = { r.value("x", 0.0f), r.value("y", 0.0f), r.value("z", 0.0f),
								  r.value("w", 1.0f) };
			}
			if (j.contains("scale")) {
				const auto& s = j["scale"];
				part.scale = { s.value("x", 1.0f), s.value("y", 1.0f), s.value("z", 1.0f) };
			}
			if (j.contains("components") && j["components"].is_object()) {
				part.components = j["components"];
			}
			return part;
		}

		std::vector<TemplatePart> ListParts(const EditorState& state, const std::string& name) {
			std::vector<TemplatePart> result;
			if (state.world == nullptr) {
				return result;
			}
			const nlohmann::json* parts = state.world->GetTemplateParts(name);
			if (parts == nullptr || !parts->is_array()) {
				return result;
			}
			for (const auto& part : *parts) {
				if (part.is_object()) {
					result.push_back(PartFromJson(part));
				}
			}
			return result;
		}

		bool IsComposed(const EditorState& state, const std::string& name) {
			return state.world != nullptr && state.world->IsComposedTemplate(name);
		}

		std::vector<std::string> ListComposableTemplates(const EditorState& state,
			const std::string& name) {
			std::vector<std::string> result;
			if (state.world == nullptr) {
				return result;
			}
			for (const std::string& candidate : ListAuthored(state)) {
				if (state.world->CanComposeTemplate(name, candidate)) {
					result.push_back(candidate);
				}
			}
			return result;
		}

		//A part name not already used in this template, derived from the template the
		//part is. Two swords on one troll are a normal thing to want, and the second one
		//cannot silently overwrite the first - the name is what addresses the spawned
		//entity.
		static std::string UniquePartName(const std::vector<TemplatePart>& parts,
			const std::string& base) {
			std::string candidate = base;
			int suffix = 2;
			bool taken = true;
			while (taken) {
				taken = false;
				for (const TemplatePart& part : parts) {
					if (part.name == candidate) {
						taken = true;
						break;
					}
				}
				if (taken) {
					candidate = base + std::to_string(suffix++);
				}
			}
			return candidate;
		}

		bool AddPart(EditorState& state, const std::string& name, const std::string& part_template,
			std::string& error, std::string* out_part_name) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			if (!IsAuthored(state, name)) {
				error = "unknown template: " + name;
				return false;
			}
			if (!state.world->IsTemplateLoaded(part_template)) {
				error = "unknown template: " + part_template;
				return false;
			}
			if (!state.world->CanComposeTemplate(name, part_template)) {
				error = part_template + " already contains " + name +
					", so adding it would compose a template into itself";
				return false;
			}
			TemplateSnapshot before;
			GetSnapshot(state, name, before);

			TemplatePart part;
			part.name = UniquePartName(ListParts(state, name), part_template);
			part.template_name = part_template;

			nlohmann::json parts = before.parts;
			if (!parts.is_array()) {
				parts = nlohmann::json::array();
			}
			parts.push_back(PartToJson(part));
			if (!MutateTemplate(state, name, before.components, false, error, &parts)) {
				return false;
			}
			if (out_part_name != nullptr) {
				*out_part_name = part.name;
			}
			state.status_message = "Added part '" + part.name + "' to " + name;
			return true;
		}

		bool RemovePart(EditorState& state, const std::string& name, const std::string& part_name,
			std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before) || !before.exists) {
				error = "unknown template: " + name;
				return false;
			}
			nlohmann::json parts = nlohmann::json::array();
			bool found = false;
			for (const auto& part : before.parts) {
				if (part.is_object() && part.value("name", std::string()) == part_name) {
					found = true;
					continue;
				}
				parts.push_back(part);
			}
			if (!found) {
				error = "unknown part: " + part_name;
				return false;
			}
			if (!MutateTemplate(state, name, before.components, false, error, &parts)) {
				return false;
			}
			state.status_message = "Removed part '" + part_name + "' from " + name;
			return true;
		}

		//The shared body of SetPart/ApplyPart: replace one entry, optionally recording.
		static bool WritePart(EditorState& state, const std::string& name,
			const std::string& part_name, const TemplatePart& part, bool record,
			std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before) || !before.exists) {
				error = "unknown template: " + name;
				return false;
			}
			nlohmann::json parts = before.parts;
			nlohmann::json* entry = FindPartEntry(parts, part_name);
			if (entry == nullptr) {
				error = "unknown part: " + part_name;
				return false;
			}
			if (state.world != nullptr && !part.template_name.empty() &&
				!state.world->CanComposeTemplate(name, part.template_name)) {
				error = part.template_name + " cannot be a part of " + name;
				return false;
			}
			//Renaming a part to one that is taken would make two entries address the same
			//spawned entity, and the second would silently win.
			if (part.name != part_name) {
				for (const TemplatePart& existing : ListParts(state, name)) {
					if (existing.name == part.name) {
						error = "a part named '" + part.name + "' already exists";
						return false;
					}
				}
			}
			*entry = PartToJson(part);
			if (record) {
				return MutateTemplate(state, name, before.components, false, error, &parts);
			}
			TemplateSnapshot target = before;
			target.parts = parts;
			return ApplySnapshot(state, name, target, error);
		}

		bool SetPart(EditorState& state, const std::string& name, const std::string& part_name,
			const TemplatePart& part, std::string& error) {
			return WritePart(state, name, part_name, part, true, error);
		}

		bool ApplyPart(EditorState& state, const std::string& name, const std::string& part_name,
			const TemplatePart& part, std::string& error) {
			return WritePart(state, name, part_name, part, false, error);
		}

		std::vector<std::string> ListRootBones(const EditorState& state, const std::string& name) {
			std::vector<std::string> bones;
			if (state.world == nullptr) {
				return bones;
			}
			Coordinator* tc = state.world->GetTemplatesCoordinator();
			Entity te = state.world->GetTemplateEntity(name);
			if (tc == nullptr || te == INVALID_ENTITY_ID || !tc->ContainsComponent<Mesh>(te)) {
				return bones;
			}
			return tc->GetComponent<Mesh>(te).GetJointNames();
		}

		bool SetStorage(EditorState& state, const std::string& name, bool inline_in_level,
			std::string& error) {
			TemplateSnapshot before;
			if (!GetSnapshot(state, name, before) || !before.exists) {
				error = "unknown template: " + name;
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

		//The component block a scene entity makes as a template: every registered
		//component it has that can be rebuilt from JSON, starting at the origin.
		//
		//A template is a *kind* of object, not a placement of one: it keeps the entity's
		//rotation and scale (those are part of how the object looks) but not its
		//position, so every instance is positioned by where it is placed rather than
		//piling up on the source entity's spot.
		static nlohmann::json SerializeEntityAsTemplate(EditorState& state, Coordinator* c,
			Entity e) {
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
			if (components.contains(Transform::NAME)) {
				components[Transform::NAME]["position"] = nlohmann::json{
					{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f} };
			}
			//A part of the selection is about to become a part of a composed template,
			//where the parent link is rebuilt by the spawner from the parts list. Carrying
			//the old one into the definition would make every instance point at an entity
			//of the level it was authored in.
			if (components.contains(Base::NAME)) {
				components[Base::NAME].erase("parent");
				components[Base::NAME].erase("parent_bone");
			}
			return components;
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

			nlohmann::json components = SerializeEntityAsTemplate(state, c, e);

			if (!MutateTemplate(state, template_name, components, false, error)) {
				return false;
			}
			state.selected_template = template_name;
			state.status_message = "Created template '" + template_name + "' from " + entity_name;
			return true;
		}

		std::string UniqueTemplateName(const EditorState& state, const std::string& base) {
			if (state.world == nullptr) {
				return base;
			}
			std::string candidate = base;
			int suffix = 2;
			while (state.world->IsTemplateLoaded(candidate)) {
				candidate = base + std::to_string(suffix++);
			}
			return candidate;
		}

		//The template an entity was placed from, or "" when it is not a placed instance
		//(an FBX-authored entity, a clone, or a part of some other instance).
		static std::string InstanceTemplateOf(const EditorState& state, const std::string& entity_name) {
			for (const PlacedInstance& inst : state.placed_instances) {
				if (inst.name == entity_name) {
					return inst.template_name;
				}
			}
			return std::string();
		}

		//An entity's pose expressed in `root`'s frame, then with the part template's own
		//base transform taken out - because SpawnInstance will add that back, exactly as
		//it does for an instance record (see Inspector::StoreInstanceTransform, which
		//undoes the same composition for the same reason).
		static void MeasurePart(EditorState& state, const Transform& root, const Transform& t,
			const std::string& part_template, TemplatePart& part) {
			const float4 inverse_root = quaternion_conjugate(root.rotation);
			vector3d offset = DirectX::XMVector3Transform(
				DirectX::XMVectorSet(t.position.x - root.position.x, t.position.y - root.position.y,
					t.position.z - root.position.z, 1.0f),
				DirectX::XMMatrixRotationQuaternion(XMLoadFloat4(&inverse_root)));
			float3 in_root_frame{};
			DirectX::XMStoreFloat3(&in_root_frame, offset);
			//World = local, then the parent's rotation - so the local rotation is the
			//parent's taken off the *left*, which is not what
			//express_rotation_with_respect_to does (that is the right-hand division the
			//instance records need).
			const float4 rotation_in_root = quaternion_multiply(inverse_root, t.rotation);

			float3 base_position{};
			float4 base_rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
			float3 base_scale{ 1.0f, 1.0f, 1.0f };
			state.world->GetTemplateBaseTransform(part_template, base_position, base_rotation,
				base_scale);
			part.position = SUB_F3_F3(in_root_frame, base_position);
			part.rotation = express_rotation_with_respect_to(rotation_in_root, base_rotation);
			part.scale = {
				(base_scale.x != 0.0f) ? t.scale.x / base_scale.x : t.scale.x,
				(base_scale.y != 0.0f) ? t.scale.y / base_scale.y : t.scale.y,
				(base_scale.z != 0.0f) ? t.scale.z / base_scale.z : t.scale.z,
			};
		}

		bool CreateFromSelection(EditorState& state, const std::vector<std::string>& entity_names,
			const std::string& root_entity, bool pivot_root, const std::string& template_name,
			std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				error = "no scene";
				return false;
			}
			if (template_name.empty() || state.world->IsTemplateLoaded(template_name)) {
				error = "a template named '" + template_name + "' already exists";
				return false;
			}
			Entity root = c->GetEntityByName(root_entity);
			if (root == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(root)) {
				error = "root entity not found: " + root_entity;
				return false;
			}
			if (entity_names.size() < 2 && !pivot_root) {
				error = "a composed template needs more than one entity";
				return false;
			}
			//Copied, not referenced: the sub-templates created below can move the
			//coordinator's component storage, and every offset is measured against the
			//pose the root had when the selection was made either way.
			const Transform root_transform = c->GetConstComponent<Transform>(root);

			nlohmann::json components;
			if (pivot_root) {
				//No one of the pieces *is* the object, so the template's own body is an
				//invisible marker at the root's pose: something to select, move and
				//rotate the assembly by. It keeps the default cube CreateTemplate gives
				//every template - hidden rather than absent, because a template with no
				//mesh is not spawnable at all (see World::SpawnInstance).
				components[Base::NAME] = { {"visible", false}, {"cast_shadow", false},
										   {"draw_depth", false} };
				components[Transform::NAME] = {
					{"position", {{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}}},
					{"rotation", {{"x", root_transform.rotation.x}, {"y", root_transform.rotation.y},
								  {"z", root_transform.rotation.z}, {"w", root_transform.rotation.w}}},
					{"scale", {{"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}}} };
			}
			else {
				if (!c->ContainsComponent<Mesh>(root)) {
					error = root_entity + " has no Mesh, so it cannot be the root of a template";
					return false;
				}
				components = SerializeEntityAsTemplate(state, c, root);
			}

			nlohmann::json parts = nlohmann::json::array();
			std::vector<TemplatePart> added;
			int made_templates = 0;
			for (const std::string& entity_name : entity_names) {
				if (!pivot_root && entity_name == root_entity) {
					continue;
				}
				Entity e = c->GetEntityByName(entity_name);
				if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e) ||
					!c->ContainsComponent<Mesh>(e)) {
					//Lights, cameras and the sky are not objects a template can carry.
					continue;
				}
				//A part is a reference, so there has to be something to refer to: an
				//entity placed from a template names that one, anything else gets a
				//template of its own made from it first. Each of those is its own
				//undoable step, undone after this one by the LIFO stack.
				std::string part_template = InstanceTemplateOf(state, entity_name);
				if (part_template.empty()) {
					part_template = UniqueTemplateName(state, entity_name);
					std::string sub_error;
					if (!CreateFromEntity(state, entity_name, part_template, sub_error)) {
						printf("TemplateOps::CreateFromSelection: %s\n", sub_error.c_str());
						continue;
					}
					++made_templates;
				}
				TemplatePart part;
				part.name = UniquePartName(added, entity_name);
				part.template_name = part_template;
				MeasurePart(state, root_transform, c->GetConstComponent<Transform>(e),
					part_template, part);
				added.push_back(part);
				parts.push_back(PartToJson(part));
			}
			if (parts.empty()) {
				error = "nothing in the selection could become a part";
				return false;
			}
			if (!MutateTemplate(state, template_name, components, false, error, &parts)) {
				return false;
			}
			state.selected_template = template_name;
			state.status_message = "Created composed template '" + template_name + "' from " +
				std::to_string(parts.size()) + " part(s)" +
				(made_templates > 0 ? " (" + std::to_string(made_templates) + " new sub-template(s))" : "");
			return true;
		}

		std::string InstanceOf(const EditorState& state, const std::string& entity_name) {
			for (const PlacedInstance& inst : state.placed_instances) {
				if (inst.name == entity_name) {
					return inst.name;
				}
				//"<instance>__<part>", the naming World::SpawnInstance gives a composed
				//instance's parts. Checked as a prefix so a part of a part resolves to
				//the instance at the top too.
				const std::string prefix = inst.name + World::PART_NAME_SEPARATOR;
				if (entity_name.compare(0, prefix.size(), prefix) == 0) {
					return inst.name;
				}
			}
			return std::string();
		}

		//Component-space division, guarding the axis a zero scale would blow up. A
		//degenerate instance scale is already meaningless; keeping the value is a better
		//answer than an infinity written into the template file.
		static float3 UnscaleBy(const float3& value, const float3& scale) {
			return {
				(scale.x != 0.0f) ? value.x / scale.x : value.x,
				(scale.y != 0.0f) ? value.y / scale.y : value.y,
				(scale.z != 0.0f) ? value.z / scale.z : value.z,
			};
		}

		//The inverse of what World::SpawnInstance did to this part: take the spawned
		//entity's live Transform back to the values the template's parts list holds.
		//
		//Three compositions to undo, in the order the spawner applied them: the root's
		//pose (detached parts only - an attached part's Transform is already an offset),
		//the instance's scale (which a bone-riding part never carried, since the parent's
		//world matrix scales it instead), and the part template's own base transform,
		//which SpawnTemplateEntities adds to every spawn.
		static void MeasureSpawnedPart(EditorState& state, const Transform& root,
			const Transform& spawned, const float3& instance_scale, TemplatePart& part) {
			float3 position = spawned.position;
			float4 rotation = spawned.rotation;
			float3 scale = spawned.scale;

			float3 base_position{};
			float4 base_rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
			float3 base_scale{ 1.0f, 1.0f, 1.0f };
			state.world->GetTemplateBaseTransform(part.template_name, base_position,
				base_rotation, base_scale);

			if (!part.attach) {
				//Composed into world space at spawn: back into the root's frame first.
				const float4 inverse_root = quaternion_conjugate(root.rotation);
				const float3 relative = SUB_F3_F3(SUB_F3_F3(position, base_position), root.position);
				vector3d offset = DirectX::XMVector3Transform(
					DirectX::XMVectorSet(relative.x, relative.y, relative.z, 1.0f),
					DirectX::XMMatrixRotationQuaternion(XMLoadFloat4(&inverse_root)));
				DirectX::XMStoreFloat3(&position, offset);
				rotation = quaternion_multiply(inverse_root, rotation);
			}
			else {
				position = SUB_F3_F3(position, base_position);
			}
			if (part.attach && !part.bone.empty()) {
				part.position = position;
			}
			else {
				part.position = UnscaleBy(position, instance_scale);
			}
			part.rotation = express_rotation_with_respect_to(rotation, base_rotation);
			scale = UnscaleBy(scale, base_scale);
			part.scale = (part.attach && !part.bone.empty())
				? scale : UnscaleBy(scale, instance_scale);
		}

		bool ApplyInstanceToTemplate(EditorState& state, const std::string& instance_name,
			std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				error = "no scene";
				return false;
			}
			const std::string instance = InstanceOf(state, instance_name);
			if (instance.empty()) {
				error = instance_name + " is not a placed instance";
				return false;
			}
			const PlacedInstance* record = nullptr;
			for (const PlacedInstance& inst : state.placed_instances) {
				if (inst.name == instance) {
					record = &inst;
					break;
				}
			}
			if (record == nullptr) {
				error = "no record for instance: " + instance;
				return false;
			}
			const std::string template_name = record->template_name;
			if (!IsAuthored(state, template_name)) {
				error = "not an authored template: " + template_name;
				return false;
			}
			Entity root = c->GetEntityByName(instance);
			if (root == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(root)) {
				error = "instance entity not found: " + instance;
				return false;
			}
			const Transform root_transform = c->GetConstComponent<Transform>(root);
			const float3 instance_scale = record->scale;

			TemplateSnapshot before;
			if (!GetSnapshot(state, template_name, before) || !before.exists) {
				error = "unknown template: " + template_name;
				return false;
			}
			//Captured before anything is erased, so one undo puts back both halves of
			//what this does.
			std::vector<std::pair<std::string, ComponentDelta>> deltas_before;
			std::vector<std::string> overridden_before;

			nlohmann::json parts = before.parts;
			int applied = 0;
			for (auto& entry : parts) {
				if (!entry.is_object()) {
					continue;
				}
				TemplatePart part = PartFromJson(entry);
				const std::string entity_name = instance + World::PART_NAME_SEPARATOR + part.name;
				Entity e = c->GetEntityByName(entity_name);
				if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e)) {
					//A part the level removed from this instance: the template keeps what
					//it has rather than being edited by an absence.
					continue;
				}
				MeasureSpawnedPart(state, root_transform, c->GetConstComponent<Transform>(e),
					instance_scale, part);
				//Component edits made to this part travel with it: the point of applying
				//is that what you see on this instance is what the template becomes.
				auto delta = state.component_deltas.find(entity_name);
				if (delta != state.component_deltas.end()) {
					deltas_before.push_back({ entity_name, delta->second });
					for (const auto& [component, value] : delta->second.added) {
						part.components[component] = value;
					}
					for (const std::string& removed : delta->second.removed) {
						part.components.erase(removed);
					}
					state.component_deltas.erase(delta);
				}
				if (state.overridden_entities.erase(entity_name) != 0) {
					overridden_before.push_back(entity_name);
				}
				entry = PartToJson(part);
				++applied;
			}
			if (applied == 0) {
				error = instance + " has no parts to apply";
				return false;
			}

			TemplateSnapshot target = before;
			target.parts = parts;
			if (!ApplySnapshot(state, template_name, target, error)) {
				return false;
			}
			TemplateSnapshot after;
			GetSnapshot(state, template_name, after);
			EditorHistory::Push({
				"apply " + instance + " to template " + template_name,
				[template_name, before, deltas_before, overridden_before](EditorState& s) {
					std::string err;
					ApplySnapshot(s, template_name, before, err);
					for (const auto& [name, delta] : deltas_before) {
						s.component_deltas[name] = delta;
					}
					for (const std::string& name : overridden_before) {
						s.overridden_entities.insert(name);
					}
				},
				[template_name, after, deltas_before, overridden_before](EditorState& s) {
					std::string err;
					ApplySnapshot(s, template_name, after, err);
					for (const auto& [name, delta] : deltas_before) {
						s.component_deltas.erase(name);
					}
					for (const std::string& name : overridden_before) {
						s.overridden_entities.erase(name);
					}
				} });

			state.selected_template = template_name;
			state.status_message = "Applied " + std::to_string(applied) + " part(s) of " +
				instance + " to template " + template_name;
			return true;
		}

		bool CreateFromModel(EditorState& state, const std::string& model_name,
			const std::string& template_name, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			const World::ModelAssets* assets = state.world->GetModelAssets(model_name);
			if (assets == nullptr) {
				error = "unknown model: " + model_name;
				return false;
			}
			if (template_name.empty() || state.world->IsTemplateLoaded(template_name)) {
				error = "a template named '" + template_name + "' already exists";
				return false;
			}
			//A Gaussian splat cloud is renderable geometry with no node behind it: a
			//.ply registers a cloud and no entities at all, so the mesh-node search
			//below would report it as an animation-only model and refuse. It needs no
			//node - the cloud *is* the geometry, and there is no exported transform to
			//preserve, so the template is the component plus an identity transform.
			if (!assets->splat_clouds.empty()) {
				nlohmann::json splat_components = nlohmann::json::object();
				splat_components[Components::SplatCloud::NAME] = nlohmann::json{
					{"name", assets->splat_clouds.front()} };
				splat_components[Transform::NAME] = nlohmann::json{
					{"position", {{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}}},
					{"rotation", {{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 1.0f}}},
					{"scale", {{"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}}} };
				if (!MutateTemplate(state, template_name, splat_components, false, error)) {
					return false;
				}
				state.selected_template = template_name;
				state.status_message = "Created template '" + template_name +
					"' from splat cloud " + model_name;
				return true;
			}

			//The first renderable node of the model: an .fbx registers armatures and
			//empties alongside its meshes, and a template is built from geometry.
			Coordinator* tc = state.world->GetTemplatesCoordinator();
			Entity source = INVALID_ENTITY_ID;
			for (Entity e : state.world->GetModelEntities(model_name)) {
				if (tc != nullptr && tc->ContainsComponent<Mesh>(e) &&
					tc->ContainsComponent<Transform>(e)) {
					source = e;
					break;
				}
			}
			if (source == INVALID_ENTITY_ID) {
				//An animation-only .fbx is a perfectly good model and a perfectly
				//impossible object; say which of the two this is rather than failing
				//with something generic.
				error = model_name + " has no mesh - it is an animation-only model, so "
					"there is nothing to build an object out of. Add its clips to a "
					"template that does have a mesh.";
				return false;
			}

			nlohmann::json components = nlohmann::json::object();
			SerializeContext ctx;
			ctx.world = state.world;
			ctx.coordinator = tc;
			//Mesh and Material read off the node, so a multi-material .fbx keeps the
			//pairing the artist exported rather than the first material in the file.
			try {
				components[Mesh::NAME] = ComponentRegistry::Instance().Find(Mesh::NAME)->serialize(ctx, source);
			}
			catch (const std::exception&) {
			}
			//Only the mesh name is wanted here: whatever animation the node happens to
			//have landed on is not a decision this template has made yet.
			if (components.contains(Mesh::NAME)) {
				components[Mesh::NAME].erase("animation");
				components[Mesh::NAME].erase("animation_loop");
				components[Mesh::NAME].erase("animation_speed");
				components[Mesh::NAME].erase("skeletons");
				components[Mesh::NAME].erase("clips");
			}
			if (tc->ContainsComponent<Material>(source)) {
				try {
					components[Material::NAME] =
						ComponentRegistry::Instance().Find(Material::NAME)->serialize(ctx, source);
				}
				catch (const std::exception&) {
				}
			}
			//The node's own rotation and scale: an .fbx exported in centimetres carries
			//its 0.025 there, and a template that dropped it would place objects forty
			//times too big. The position is not kept - where an object goes is what
			//placing an instance decides.
			const Transform& t = tc->GetConstComponent<Transform>(source);
			components[Transform::NAME] = nlohmann::json{
				{"position", {{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}}},
				{"rotation", {{"x", t.rotation.x}, {"y", t.rotation.y}, {"z", t.rotation.z},
							  {"w", t.rotation.w}}},
				{"scale", {{"x", t.scale.x}, {"y", t.scale.y}, {"z", t.scale.z}}} };

			if (!MutateTemplate(state, template_name, components, false, error)) {
				return false;
			}
			state.selected_template = template_name;
			const size_t parts = state.world->GetModelEntities(model_name).size();
			state.status_message = "Created template '" + template_name + "' from model " + model_name;
			if (parts > 1) {
				//Said plainly rather than silently taking part 0: a multi-node .fbx is
				//several objects, and which ones belong together is the author's call.
				state.status_message += " (used its first mesh node; the model has " +
					std::to_string(parts) + " nodes)";
			}
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
				error = "unknown template: " + source;
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
				error = "unknown template: " + name;
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
			//Start from the whole template, not just its components: parts and storage
			//are orthogonal to the block being edited, and a snapshot built without
			//them decomposes a composed template and moves it back into a .tpl on the
			//next widget drag. This is the same rule MutateTemplate states.
			TemplateSnapshot target;
			if (!GetSnapshot(state, name, target)) {
				error = "no world";
				return false;
			}
			if (!target.exists) {
				error = "unknown template: " + name;
				return false;
			}
			target.components[component] = value;
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
				error = "unknown template: " + name;
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
				error = "unknown template: " + name;
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

		bool SetMesh(EditorState& state, const std::string& name,
			const std::string& mesh_name, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			block["name"] = mesh_name;
			//The animation library survives a mesh swap on purpose. A library entry is
			//a role this object plays, and the clips are attached to whatever mesh the
			//template ends up with - swapping a character's mesh for a re-export is
			//precisely the case where re-authoring "idle"/"walk"/"attack" by hand would
			//be pure loss. Clips that the new rig cannot play show as unresolved in the
			//Animations section rather than being silently dropped.
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool SetMaterial(EditorState& state, const std::string& name,
			const std::string& material_name, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Material::NAME);
			block["name"] = material_name;
			return SetComponent(state, name, Material::NAME, block, error);
		}

		bool AddClip(EditorState& state, const std::string& name, const std::string& logical,
			const std::string& clip, std::string& error) {
			if (state.world == nullptr) {
				error = "no world";
				return false;
			}
			if (logical.empty()) {
				error = "animation name is empty";
				return false;
			}
			if (state.world->FindAnimationSet(clip).empty()) {
				error = "no imported model offers the clip '" + clip + "'";
				return false;
			}
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			nlohmann::json library = (block.contains("clips") && block["clips"].is_object())
				? block["clips"] : nlohmann::json::object();
			const bool first = library.empty();
			library[logical] = clip;
			block["clips"] = library;
			//A library of one with nothing selected is a template that has an animation
			//and stands still, which is never what adding the first one meant.
			if (first && block.value("animation", std::string()).empty()) {
				block["animation"] = logical;
				block["animation_loop"] = true;
				block["animation_speed"] = 1.0f;
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool RemoveClip(EditorState& state, const std::string& name, const std::string& logical,
			std::string& error) {
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			if (!block.contains("clips") || !block["clips"].is_object() ||
				!block["clips"].contains(logical)) {
				error = name + " has no animation named '" + logical + "'";
				return false;
			}
			block["clips"].erase(logical);
			if (block["clips"].empty()) {
				block.erase("clips");
			}
			//The default pointed at the entry being removed: an "animation" naming
			//nothing would leave the template claiming an animation it cannot play.
			if (block.value("animation", std::string()) == logical) {
				block.erase("animation");
				block.erase("animation_loop");
				block.erase("animation_speed");
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool RenameClip(EditorState& state, const std::string& name, const std::string& logical,
			const std::string& new_logical, std::string& error) {
			if (new_logical.empty()) {
				error = "animation name is empty";
				return false;
			}
			if (new_logical == logical) {
				return true;
			}
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			if (!block.contains("clips") || !block["clips"].is_object() ||
				!block["clips"].contains(logical)) {
				error = name + " has no animation named '" + logical + "'";
				return false;
			}
			if (block["clips"].contains(new_logical)) {
				error = name + " already has an animation named '" + new_logical + "'";
				return false;
			}
			block["clips"][new_logical] = block["clips"][logical];
			block["clips"].erase(logical);
			//The default is stored as a logical name, so renaming one has to carry it.
			if (block.value("animation", std::string()) == logical) {
				block["animation"] = new_logical;
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
		}

		bool SetDefaultClip(EditorState& state, const std::string& name,
			const std::string& logical, bool loop, float speed, std::string& error) {
			nlohmann::json block = GetComponent(state, name, Mesh::NAME);
			if (logical.empty()) {
				//Stand still. Written as an explicit empty name rather than by dropping
				//the key: Mesh::FromJson reads "" as StopAnimation and a missing key as
				//"whatever the mesh defaults to", and those are different templates.
				block["animation"] = "";
				block.erase("animation_loop");
				block.erase("animation_speed");
			}
			else {
				const bool known = block.contains("clips") && block["clips"].is_object() &&
					block["clips"].contains(logical);
				if (!known) {
					error = name + " has no animation named '" + logical + "'";
					return false;
				}
				block["animation"] = logical;
				block["animation_loop"] = loop;
				block["animation_speed"] = speed;
			}
			return SetComponent(state, name, Mesh::NAME, block, error);
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
			nlohmann::json parts;
			if (!state.world->ReadTemplateFile(tpl_path, false, name, components, parts, error)) {
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

			if (!MutateTemplate(state, name, components, false, error, &parts)) {
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
					//Deliberately RegisterAsset and not ApplySnapshot: discovering a
					//template on disk is not an edit, so it must not mark the file dirty.
					RegisterAsset(state, name);
				}
			}

			//Templates the level's own "templates" array pulled in live only in the
			//World until now (an inline one, or a .tpl kept outside Assets/Templates).
			//List them all, so "what this level can place" is one answer rather than two.
			for (const std::string& name : state.world->ListTemplates()) {
				if (FindAsset(state, name) != nullptr) {
					continue;
				}
				TemplateAsset asset;
				asset.name = name;
				asset.loaded = true;
				asset.file_path = TemplateFilePath(state, name);
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

				//The animations live in their own section above rather than in here.
				//They are a list with names, sources and a default - a table's worth of
				//authoring - and burying that under "Mesh" is what made animations feel
				//like a property of the imported file instead of part of the object.
				const int clip_count = (int)TemplateOps::ListClips(state, name).size();
				if (clip_count == 0) {
					ImGui::TextDisabled("No animations yet - see the Animations section above.");
				}
				else {
					ImGui::TextDisabled("%d animation(s) - see the Animations section above.",
						clip_count);
				}
			}

			//== The Animations screen ==========================================
			//
			//A template's animation library: the names this object answers to, the
			//imported clip behind each one, which is the default, and a way to hear
			//what a clip actually looks like before committing to it.
			//
			//It is a section of the Templates panel rather than a panel of its own
			//because an animation is not an asset you manage - it is part of what the
			//object *is*, and it is only meaningful next to the mesh it plays on.

			//Which clip the preview is auditioning, and for which template. Panel
			//state, not template data: auditioning must not edit anything.
			std::string audition_template;
			std::string audition_clip;

			//The Add popup's staging: which clip is picked and what it will be called.
			std::string add_clip_source;
			char add_clip_name[64] = "";

			//"troll_walk" under the model "troll_walk" is worth nothing as a name; the
			//part that distinguishes it is. Seeds the name field so the common case is
			//"click clip, press Add".
			std::string SuggestClipName(const std::string& clip, const std::string& model) {
				std::string suggestion = clip;
				//Trim a shared prefix with the model name ("troll_walk" under model
				//"troll_tpose" -> "walk"), which is how these files are named in
				//practice: <character>_<action>.fbx.
				size_t common = 0;
				while (common < suggestion.size() && common < model.size() &&
					suggestion[common] == model[common]) {
					++common;
				}
				//Only at a separator, so "troll_attack"/"troll_tpose" does not become
				//"ttack".
				while (common > 0 && suggestion[common - 1] != '_' && suggestion[common - 1] != '-') {
					--common;
				}
				if (common > 0 && common < suggestion.size()) {
					suggestion = suggestion.substr(common);
				}
				return suggestion;
			}

			void DrawAddClipPopup(EditorState& state, const std::string& name) {
				if (!ImGui::BeginPopup("add_template_clip")) {
					return;
				}
				const std::vector<TemplateOps::AvailableClip> available =
					TemplateOps::ListAvailableClips(state);
				if (available.empty()) {
					ImGui::TextDisabled("No imported model carries any animation.");
					ImGui::TextDisabled("Import one with File/Import Model...");
					ImGui::EndPopup();
					return;
				}

				ImGui::TextUnformatted("Animation to add:");
				if (ImGui::BeginChild("##clip_list", ImVec2(320.0f, 220.0f), ImGuiChildFlags_Border)) {
					std::string current_model;
					bool model_open = false;
					for (const TemplateOps::AvailableClip& entry : available) {
						if (entry.model != current_model) {
							if (model_open) {
								ImGui::TreePop();
							}
							current_model = entry.model;
							model_open = ImGui::TreeNodeEx(current_model.c_str(),
								ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
						}
						if (!model_open) {
							continue;
						}
						ImGui::PushID(entry.clip.c_str());
						if (ImGui::Selectable(entry.clip.c_str(), entry.clip == add_clip_source)) {
							add_clip_source = entry.clip;
							strncpy_s(add_clip_name, SuggestClipName(entry.clip, entry.model).c_str(),
								sizeof(add_clip_name) - 1);
						}
						ImGui::PopID();
					}
					if (model_open) {
						ImGui::TreePop();
					}
				}
				ImGui::EndChild();

				ImGui::SetNextItemWidth(200.0f);
				ImGui::InputText("Call it", add_clip_name, sizeof(add_clip_name));
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("The name this object knows the animation by.\n"
						"Game code plays it with SetAnimation(\"%s\").",
						add_clip_name[0] != '\0' ? add_clip_name : "walk");
				}
				ImGui::BeginDisabled(add_clip_source.empty() || add_clip_name[0] == '\0');
				if (ImGui::Button("Add", ImVec2(90.0f, 0.0f))) {
					std::string error;
					if (!TemplateOps::AddClip(state, name, add_clip_name, add_clip_source, error)) {
						state.status_message = "Add animation failed: " + error;
					}
					else {
						state.status_message = "Added animation '" + std::string(add_clip_name) +
							"' (" + add_clip_source + ") to " + name;
						add_clip_source.clear();
						add_clip_name[0] = '\0';
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Close", ImVec2(90.0f, 0.0f))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			void DrawAnimationsSection(EditorState& state, const std::string& name) {
				ImGui::PushID("Animations");
				if (!ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::PopID();
					return;
				}
				ImGui::PushID("body");

				const std::vector<TemplateOps::TemplateClip> clips =
					TemplateOps::ListClips(state, name);
				const nlohmann::json mesh_block = TemplateOps::GetComponent(state, name, Mesh::NAME);
				bool loop = mesh_block.value("animation_loop", true);
				float speed = mesh_block.value("animation_speed", 1.0f);
				const std::string current = mesh_block.value("animation", std::string());

				if (clips.empty()) {
					ImGui::TextWrapped("This template has no animations. Add one and it becomes "
						"part of the object: instances play it, and game code asks for it by "
						"the name you give it here.");
				}
				else if (ImGui::BeginTable("##clips", 5,
					ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
					ImGuiTableFlags_RowBg)) {
					ImGui::TableSetupColumn("Plays", ImGuiTableColumnFlags_WidthFixed, 44.0f);
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("Clip");
					ImGui::TableSetupColumn("From model");
					ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 96.0f);
					ImGui::TableHeadersRow();

					for (const TemplateOps::TemplateClip& clip : clips) {
						ImGui::PushID(clip.name.c_str());
						ImGui::TableNextRow();

						//Audition: play this clip in the preview without touching the
						//template. The default is set by the radio in the Name column,
						//so hearing a clip and choosing it stay separate acts.
						ImGui::TableSetColumnIndex(0);
						const bool auditioning = (audition_template == name &&
							audition_clip == clip.name);
						if (ImGui::SmallButton(auditioning ? "Stop" : "Play")) {
							audition_template = name;
							audition_clip = auditioning ? std::string() : clip.name;
						}

						ImGui::TableSetColumnIndex(1);
						//Rename in place: the logical name is the whole point of the
						//library, and it is the field most likely to be got wrong first.
						char buffer[64];
						strncpy_s(buffer, clip.name.c_str(), sizeof(buffer) - 1);
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::InputText("##name", buffer, sizeof(buffer),
							ImGuiInputTextFlags_EnterReturnsTrue) && buffer[0] != '\0') {
							std::string error;
							if (!TemplateOps::RenameClip(state, name, clip.name, buffer, error)) {
								state.status_message = "Rename animation failed: " + error;
							}
						}

						ImGui::TableSetColumnIndex(2);
						if (clip.resolved) {
							ImGui::TextUnformatted(clip.clip.c_str());
						}
						else {
							//Broken rather than absent: the clip is still in the file,
							//and the fix is usually to import the model that carries it.
							ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.35f, 1.0f), "%s (missing)",
								clip.clip.c_str());
							if (ImGui::IsItemHovered()) {
								ImGui::SetTooltip("No imported model offers this clip.\n"
									"Import the .fbx it came from and it resolves itself.");
							}
						}

						ImGui::TableSetColumnIndex(3);
						ImGui::TextDisabled("%s", clip.resolved ? clip.model.c_str() : "-");

						ImGui::TableSetColumnIndex(4);
						bool is_default = clip.is_default;
						if (ImGui::RadioButton("Default", is_default) && !is_default) {
							std::string error;
							if (!TemplateOps::SetDefaultClip(state, name, clip.name, loop, speed, error)) {
								state.status_message = "Set default animation failed: " + error;
							}
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("What an instance of this template starts playing");
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("X")) {
							std::string error;
							if (!TemplateOps::RemoveClip(state, name, clip.name, error)) {
								state.status_message = "Remove animation failed: " + error;
							}
							else if (audition_template == name && audition_clip == clip.name) {
								audition_clip.clear();
							}
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}

				if (ImGui::Button("Add Animation...")) {
					add_clip_source.clear();
					add_clip_name[0] = '\0';
					ImGui::OpenPopup("add_template_clip");
				}
				DrawAddClipPopup(state, name);

				if (!clips.empty()) {
					ImGui::SameLine();
					//"Stands still" is a real authoring choice (an idle prop built from
					//an animated rig), distinct from having no animations at all.
					ImGui::BeginDisabled(current.empty());
					if (ImGui::Button("None by default")) {
						std::string error;
						if (!TemplateOps::SetDefaultClip(state, name, "", loop, speed, error)) {
							state.status_message = "Set default animation failed: " + error;
						}
					}
					ImGui::EndDisabled();

					ImGui::Separator();
					ImGui::TextDisabled("Default playback");
					ImGui::BeginDisabled(current.empty());
					if (ImGui::Checkbox("Loop", &loop)) {
						std::string error;
						TemplateOps::SetDefaultClip(state, name, current, loop, speed, error);
					}
					ImGui::SetNextItemWidth(160.0f);
					const bool changed = ImGui::DragFloat("Speed", &speed, 0.01f, 0.0f, 10.0f);
					const bool activated = ImGui::IsItemActivated();
					const bool finished = ImGui::IsItemDeactivatedAfterEdit();
					if (!current.empty()) {
						nlohmann::json edited = mesh_block;
						edited["animation_speed"] = speed;
						CommitBlock(state, name, Mesh::NAME, edited, changed, activated, finished);
					}
					ImGui::EndDisabled();
				}
				ImGui::PopID();
				ImGui::PopID();
			}

			//The part whose transform is mid-drag, so the drag records one history entry
			//when it ends rather than one a frame - the same shape CommitBlock gives the
			//component editors.
			std::string dragging_part;
			TemplateOps::TemplatePart drag_before;

			void DrawPartsSection(EditorState& state, const std::string& name) {
				ImGui::PushID("Parts");
				if (!ImGui::CollapsingHeader("Parts")) {
					ImGui::PopID();
					return;
				}
				ImGui::PushID("body");

				const std::vector<TemplateOps::TemplatePart> parts = TemplateOps::ListParts(state, name);
				const std::vector<std::string> bones = TemplateOps::ListRootBones(state, name);

				if (parts.empty()) {
					ImGui::TextWrapped("Parts are other templates carried by this one: a weapon on a "
						"character, the pieces of a house. Placing this template places all of "
						"them, and an attached part follows the root wherever it goes.");
				}
				else if (ImGui::BeginTable("##parts", 4,
					ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
					ImGuiTableFlags_RowBg)) {
					ImGui::TableSetupColumn("Part");
					ImGui::TableSetupColumn("Is");
					ImGui::TableSetupColumn("Attached to");
					ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 30.0f);
					ImGui::TableHeadersRow();

					for (const TemplateOps::TemplatePart& part : parts) {
						ImGui::PushID(part.name.c_str());
						ImGui::TableNextRow();

						ImGui::TableSetColumnIndex(0);
						char buffer[64];
						strncpy_s(buffer, part.name.c_str(), sizeof(buffer) - 1);
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::InputText("##name", buffer, sizeof(buffer),
							ImGuiInputTextFlags_EnterReturnsTrue) && buffer[0] != '\0') {
							TemplateOps::TemplatePart edited = part;
							edited.name = buffer;
							std::string error;
							if (!TemplateOps::SetPart(state, name, part.name, edited, error)) {
								state.status_message = "Rename part failed: " + error;
							}
						}

						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(part.template_name.c_str());
						if (state.world != nullptr && !state.world->IsTemplateLoaded(part.template_name)) {
							ImGui::SameLine();
							ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.35f, 1.0f), "(missing)");
						}

						ImGui::TableSetColumnIndex(2);
						//What the part follows: nothing, the root, or one of its bones. One
						//control rather than a checkbox plus a picker, because "free",
						//"the root" and "the root's hand" are three answers to one question.
						const char* current = !part.attach ? "nothing (placed free)"
							: part.bone.empty() ? "the root" : part.bone.c_str();
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::BeginCombo("##attach", current)) {
							if (ImGui::Selectable("nothing (placed free)", !part.attach)) {
								TemplateOps::TemplatePart edited = part;
								edited.attach = false;
								edited.bone.clear();
								std::string error;
								TemplateOps::SetPart(state, name, part.name, edited, error);
							}
							if (ImGui::Selectable("the root", part.attach && part.bone.empty())) {
								TemplateOps::TemplatePart edited = part;
								edited.attach = true;
								edited.bone.clear();
								std::string error;
								TemplateOps::SetPart(state, name, part.name, edited, error);
							}
							for (const std::string& bone : bones) {
								if (ImGui::Selectable(bone.c_str(), part.attach && part.bone == bone)) {
									TemplateOps::TemplatePart edited = part;
									edited.attach = true;
									edited.bone = bone;
									std::string error;
									TemplateOps::SetPart(state, name, part.name, edited, error);
								}
							}
							if (bones.empty()) {
								ImGui::TextDisabled("(the root has no skeleton to ride)");
							}
							ImGui::EndCombo();
						}

						ImGui::TableSetColumnIndex(3);
						if (ImGui::SmallButton("X")) {
							std::string error;
							if (!TemplateOps::RemovePart(state, name, part.name, error)) {
								state.status_message = "Remove part failed: " + error;
							}
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}

				//The selected part's placement, below the table: three DragFloat3s per row
				//would not fit, and the offsets are the thing most often nudged rather than
				//typed once.
				for (const TemplateOps::TemplatePart& part : parts) {
					ImGui::PushID(("xf_" + part.name).c_str());
					if (ImGui::TreeNode(("Place " + part.name).c_str())) {
						TemplateOps::TemplatePart edited = part;
						float3 euler = Inspector::QuaternionToEulerDegrees(part.rotation);
						bool changed = false;
						bool activated = false;
						bool finished = false;
						auto track = [&](bool widget_changed) {
							changed |= widget_changed;
							activated |= ImGui::IsItemActivated();
							finished |= ImGui::IsItemDeactivatedAfterEdit();
						};
						track(ImGui::DragFloat3("Offset", &edited.position.x, 0.05f));
						const bool rotated = ImGui::DragFloat3("Rotation (deg)", &euler.x, 0.5f);
						track(rotated);
						track(ImGui::DragFloat3("Scale", &edited.scale.x, 0.01f));
						if (rotated) {
							edited.rotation = float3_to_quaternion(euler);
						}
						//Mid-drag edits apply so the viewport follows, but only the release
						//records - otherwise a drag would fill the undo stack with a step
						//per frame.
						if (changed) {
							if (activated || dragging_part != part.name) {
								dragging_part = part.name;
								drag_before = part;
							}
							std::string error;
							TemplateOps::ApplyPart(state, name, part.name, edited, error);
						}
						if (finished && dragging_part == part.name) {
							TemplateOps::TemplatePart after = edited;
							std::string error;
							//Put the pre-drag value back and re-apply it as one recorded
							//edit, so undo lands where the drag started.
							TemplateOps::ApplyPart(state, name, part.name, drag_before, error);
							TemplateOps::SetPart(state, name, part.name, after, error);
							dragging_part.clear();
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}

				//One step: the button opens the list of templates that can go in here and
				//picking one adds it. It used to be a combo to choose with and a button to
				//confirm, which reads as a dead button until you notice the combo - the
				//button is the thing labelled with the verb, so it is what has to do the
				//work.
				const std::vector<std::string> composable =
					TemplateOps::ListComposableTemplates(state, name);
				if (ImGui::Button("Add Part...")) {
					ImGui::OpenPopup("add_template_part");
				}
				if (ImGui::BeginPopup("add_template_part")) {
					if (composable.empty()) {
						//Nothing to offer is nearly always "this project has one template",
						//so say what to do about it rather than showing an empty list.
						ImGui::TextDisabled("No other template can go in here.");
						ImGui::TextWrapped("A part is another template. Make the piece a template "
							"of its own first - \"New...\" up top, \"Create Template\" on a model "
							"in the Asset Browser, or \"From Selection\" on something already in "
							"the scene - and it will be offered here.");
					}
					for (const std::string& option : composable) {
						if (ImGui::Selectable(option.c_str())) {
							std::string error;
							if (!TemplateOps::AddPart(state, name, option, error)) {
								state.status_message = "Add part failed: " + error;
							}
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::EndPopup();
				}

				ImGui::TextDisabled("An attached part carries no physics of its own - the root's\n"
					"body is the composed object's. Place a part free of the root\n"
					"(\"nothing\") when it needs its own collider.");
				ImGui::PopID();
				ImGui::PopID();
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

			std::string UniqueName(const EditorState& state, const std::string& base) {
				return TemplateOps::UniqueTemplateName(state, base);
			}

			//The names of the entities currently selected, primary last (the order
			//Selection keeps them in).
			std::vector<std::string> SelectedEntityNames(const EditorState& state) {
				std::vector<std::string> names;
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					return names;
				}
				for (Entity e : state.selected_entities) {
					if (c->ContainsComponent<Base>(e)) {
						names.push_back(c->GetConstComponent<Base>(e).name);
					}
				}
				return names;
			}

			//The From Selection prompt when more than one entity is selected: what is
			//being made is a composed template, and the two things that need deciding
			//are its name and which of the selected entities the rest hang off.
			std::string composed_root;
			bool composed_pivot_root = false;

			bool DrawComposedModal(EditorState& state, const std::vector<std::string>& selection,
				char* buffer, size_t buffer_size) {
				bool confirmed = false;
				const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal("Template From Selection", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::TextUnformatted("Name for the composed template:");
					ImGui::SetNextItemWidth(320.0f);
					const bool entered = ImGui::InputText("##name", buffer, buffer_size,
						ImGuiInputTextFlags_EnterReturnsTrue);

					ImGui::Spacing();
					ImGui::SetNextItemWidth(320.0f);
					if (ImGui::BeginCombo("Root", composed_root.c_str())) {
						for (const std::string& option : selection) {
							if (ImGui::Selectable(option.c_str(), option == composed_root)) {
								composed_root = option;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::Checkbox("Empty root (the pieces are all parts)", &composed_pivot_root);
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("For an assembly where no one piece is the object - a\n"
							"house, a rock formation. The template's own body is an\n"
							"invisible marker at the root's pose, to move the whole by.");
					}
					if (composed_pivot_root) {
						ImGui::TextDisabled("All %d selected objects become parts.",
							(int)selection.size());
					}
					else {
						ImGui::TextDisabled("%s is the object; the other %d become parts of it.",
							composed_root.c_str(), (int)selection.size() - 1);
					}

					ImGui::Spacing();
					ImGui::BeginDisabled(buffer[0] == '\0' || composed_root.empty());
					const bool clicked = ImGui::Button("Create", ImVec2(90.0f, 0.0f));
					ImGui::EndDisabled();
					ImGui::SameLine();
					const bool cancel = ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) ||
						ImGui::IsKeyPressed(ImGuiKey_Escape, false);
					if ((entered || clicked) && buffer[0] != '\0' && !composed_root.empty()) {
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
				//More than one entity selected means the arrangement itself is what is
				//being saved: a composed template, with the others as parts of the one
				//picked first. That is the "build it in the scene, then keep it" path.
				const std::vector<std::string> selection = SelectedEntityNames(state);
				if (from_selection) {
					if (selection.size() > 1) {
						//The root is the *first* entity picked - the object you select
						//before gathering what goes with it - not the primary, which is
						//whatever was clicked last. The Entities panel marks it, and the
						//combo in the prompt can still override it.
						const std::string root = selection.front();
						strncpy_s(from_entity_name, UniqueName(state, root + "_group").c_str(),
							sizeof(from_entity_name) - 1);
						composed_root = root;
						composed_pivot_root = false;
						ImGui::OpenPopup("Template From Selection");
					}
					else {
						strncpy_s(from_entity_name, UniqueName(state, entity_name + "_template").c_str(),
							sizeof(from_entity_name) - 1);
						ImGui::OpenPopup("Template From Entity");
					}
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
				if (DrawComposedModal(state, selection, from_entity_name, sizeof(from_entity_name))) {
					if (!TemplateOps::CreateFromSelection(state, selection, composed_root,
						composed_pivot_root, from_entity_name, error)) {
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

			//The model viewport at the top of the details pane. One View for the panel
			//rather than one per template: the angle you last looked from is the angle
			//you want when you click the next template, and carrying it across makes
			//two templates directly comparable.
			void DrawPreview(EditorState& state, const std::string& name) {
				static ModelPreview::View view;
				static bool collapsed = false;

				//Roughly 16:9 of the details pane, floored so a narrow panel still shows
				//a usable viewport and capped so it never crowds out the components.
				const float width = ImGui::GetContentRegionAvail().x;
				const float height = std::clamp(width * 0.56f, 120.0f, 260.0f);

				if (collapsed) {
					if (ImGui::SmallButton("Show preview")) {
						collapsed = false;
					}
					return;
				}

				//An audition from the Animations section overrides what the template
				//says it plays, for as long as it is running: seeing a clip before
				//committing to it is the point, so it must not be an edit.
				view.clip_override = (audition_template == name) ? audition_clip : std::string();

				ModelPreview::Draw(state, name, view, ImVec2(width, height));
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Drag to orbit, wheel to zoom, double-click to reset");
				}
				if (!view.clip_override.empty()) {
					ImGui::TextDisabled("Playing '%s' (preview only)", view.clip_override.c_str());
				}

				ImGui::Checkbox("Animate", &view.play);
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset view")) {
					const bool play = view.play;
					view = ModelPreview::View{};
					view.play = play;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Hide")) {
					collapsed = true;
				}
			}

			void DrawDetails(EditorState& state, const std::string& name) {
				ImGui::Text("%s", name.c_str());

				//Before the buttons and the component list: what the template *is* is
				//the first thing to establish, and it is what tells you whether the
				//mesh and material names below point at what you meant.
				DrawPreview(state, name);
				ImGui::Spacing();

				//Where the definition lives. Two radio buttons rather than a checkbox,
				//because "stored in the level" and "stored in a file" are both
				//first-class answers and the label has to say which file.
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
				ImGui::Separator();

				ImGui::PushItemWidth(-140.0f);
				//Above the components, directly under the preview: the animations are
				//what the object *does*, they are the section with the most authoring in
				//it, and their Play buttons drive the viewport a few lines up. Stored in
				//the Mesh block all the same - that section points here.
				DrawAnimationsSection(state, name);
				//Then what the object is *made of*, before the components that describe
				//its own body: a composed template's parts are the larger fact about it.
				DrawPartsSection(state, name);
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
			//same set of models and templates.
			AssetBrowser::EnsureAssetsScanned(state);

			DrawToolbar(state);
			ImGui::Separator();

			const std::vector<std::string> names = TemplateOps::ListAuthored(state);
			if (names.empty()) {
				ImGui::TextDisabled("This project has no templates yet.");
				ImGui::TextDisabled("Use \"New...\" to build one, \"From Selection\" to make one\n"
					"out of a scene entity, or pick a model in the Asset Browser and\n"
					"\"Create Template\".");
				ImGui::End();
				return;
			}

			const float list_width = ImGui::GetContentRegionAvail().x * 0.32f;
			if (ImGui::BeginChild("##template_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_Border)) {
				for (const std::string& name : names) {
					std::string label = name;
					if (state.dirty_templates.count(name) != 0) {
						label += "  *";
					}
					//Selecting is all this list does: clicking a template here opens it
					//for editing and nothing else. Putting one *into* the scene is the
					//Asset Browser's job - see the panel header for why the two are
					//separate.
					if (ImGui::Selectable(label.c_str(), name == state.selected_template)) {
						state.selected_template = name;
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
