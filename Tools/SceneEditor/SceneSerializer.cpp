#include "SceneSerializer.h"
#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EntityOps.h"
#include "MaterialPanel.h"
#include "PhysicsPreview.h"
#include "TemplatePanel.h"

#include <Components/Base.h>
#include <Core/Json.h>
#include <ECS/ComponentRegistry.h>
#include <World.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace SceneSerializer {

		static json Float3ToJson(const float3& v) {
			return json{ {"x", v.x}, {"y", v.y}, {"z", v.z} };
		}

		static json Float4ToJson(const float4& v) {
			return json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
		}

		//The name a clone's source will have during the level loader's "clones"
		//phase, following clone->source links to the root and mapping any parked
		//(cut) source back to its authored identity. Parked FBX/authored entities
		//still exist during that phase (they are destroyed only in the later
		//"removed_entities" phase); the repoint-on-cut invariant guarantees a live
		//clone never points at a parked clone, so this always terminates at a
		//loadable name. Returns "" if it somehow can't be resolved.
		static std::string ResolvePersistentSource(const EditorState& state,
			const std::string& source, int depth = 0)
		{
			if (depth > 4096) {
				return "";
			}
			std::string name = source;
			auto pit = state.parked_entities.find(name);
			if (pit != state.parked_entities.end()) {
				//Parked FBX/authored: use its authored name. Parked clone: unreachable
				//given the repoint invariant, but bail rather than emit a dangling ref.
				return pit->second.empty() ? std::string() : pit->second;
			}
			for (const auto& rec : state.cloned_entities) {
				if (rec.name == name) {
					return ResolvePersistentSource(state, rec.source, depth + 1);
				}
			}
			return name; //a root authored/renamed entity present during the clones phase
		}

		//Writes an entity's component delta into its level record, merging with
		//whatever the record already carried rather than replacing it: a record can
		//hold blocks this binary never registered (a game's own components), and those
		//must survive untouched.
		//
		//Added components are re-serialized from the LIVE component rather than from
		//the payload they were added with, so values the user then edited in the
		//Inspector are what get written.
		static void WriteComponentDelta(EditorState& state, Coordinator* c,
			const std::string& entity_name, json& target)
		{
			auto opaque = state.opaque_components.find(entity_name);
			if (opaque != state.opaque_components.end()) {
				for (const auto& [name, value] : opaque->second) {
					target["components"][name] = value;
				}
			}

			auto it = state.component_deltas.find(entity_name);
			if (it == state.component_deltas.end()) {
				return;
			}
			const ComponentDelta& delta = it->second;
			Entity e = c->GetEntityByName(entity_name);

			for (const auto& [name, fallback] : delta.added) {
				const ECS::ComponentDesc* desc = ECS::ComponentRegistry::Instance().Find(name);
				json value = fallback;
				if (desc != nullptr && e != INVALID_ENTITY_ID && desc->has(c, e)) {
					try {
						value = desc->serialize(state.world->MakeSerializeContext(), e);
					}
					catch (const std::exception&) {
						//Fall back to what it was added with rather than losing the entry.
					}
				}
				target["components"][name] = value;
			}

			if (!delta.removed.empty()) {
				json removed = json::array();
				for (const std::string& name : delta.removed) {
					//A component that was removed must not also be re-added by a
					//"components" block left over from an earlier save.
					if (target.contains("components")) {
						target["components"].erase(name);
					}
					removed.push_back(name);
				}
				target["remove"] = removed;
			}
			else {
				//Nothing removed any more (an undo, or a re-add): drop a stale key from
				//a previous save rather than leaving it to strip the component again.
				target.erase("remove");
			}
		}

		void Save(EditorState& state)
		{
			if (state.current_level_path.empty()) {
				state.status_message = "No level open, nothing to save.";
				return;
			}

			//Saving ends any physics preview first. A Transform holds exactly one pose,
			//and while the preview runs that pose is the *simulated* one - the entity
			//and clone sections below read it live, so saving mid-preview would persist
			//wherever gravity had dropped things. Pausing alone would not help: it
			//freezes the simulated pose rather than restoring the authored one. Ending
			//the preview rewinds every body to its baseline (PhysicsPreview.h), which is
			//what the user authored and what belongs in the file. Done here rather than
			//at the menu item so every save surface - menu, Ctrl+S, automation - gets it.
			PhysicsPreview::SetEnabled(state, false);

			//Authored templates are written first, for one reason: the "templates"
			//array below names their .tpl files, and a level that references a file
			//which was never written cannot be reloaded. Taken before the save, which
			//clears it: dropping a removed template from the level's "templates" array
			//below still has to know which ones went.
			const std::set<std::string> removed_templates = state.removed_templates;
			if (TemplateOps::HasUnsavedTemplates(state)) {
				std::string template_error;
				if (!TemplateOps::SaveTemplates(state, template_error)) {
					state.status_message = "Save failed: could not write templates: " + template_error;
					return;
				}
			}

			//Materials and multi-materials used to be left to their own explicit save
			//(File/Save Materials) on the theory that a .mat is only ever referenced by
			//name, never by a file the level needs to exist for reload - so leaving one
			//unsaved "cost nothing but the edit". In practice that edit is exactly what
			//a user saving their level expects to be safe, and nothing anywhere (not
			//even File/Exit) warns that it silently is not - a multi-material's layers
			//edited and never explicitly saved via File/Save Materials were gone on the
			//next open. Flush them here too, the same as templates just above.
			if (MaterialOps::HasUnsavedMaterials(state)) {
				std::string material_error;
				if (!MaterialOps::SaveMaterials(state, material_error)) {
					state.status_message = "Save failed: could not write materials: " + material_error;
					return;
				}
			}

			json level;
			try {
				level = json::parse(std::ifstream(state.current_level_path));
			}
			catch (std::exception&) {
				state.status_message = "Save failed: could not re-read " + state.current_level_path;
				return;
			}
			json& jw = level["world"];

			Coordinator* c = state.world->GetCoordinator();

			//Clone names never go in "entities" (they are written to "clones"); parked
			//(cut) names never go anywhere except "removed_entities"; created ones are
			//written whole to "created_entities" further down.
			std::set<std::string> clone_names;
			for (const auto& rec : state.cloned_entities) {
				clone_names.insert(rec.name);
			}
			const std::set<std::string> created_names(state.created_entities.begin(),
				state.created_entities.end());
			//Whether this entity's edits belong in a record of its own rather than in an
			//"entities" override entry. A created entity's record carries its existence,
			//its pose and its components together, so an override entry for the same name
			//would be a second, partial copy of it.
			auto has_own_record = [&](const std::string& name) {
				return clone_names.count(name) != 0 || created_names.count(name) != 0 ||
					EntityOps::IsParkedName(name);
			};
			//current name -> authored (load-time) name, for the entries below.
			std::map<std::string, std::string> authored_of;
			for (const auto& [authored, current] : state.renamed_entities) {
				authored_of[current] = authored;
			}

			//1) Per-entity overrides for existing (FBX/JSON-authored) entities that
			//   were edited: transform changes and/or a rename. Keyed by the authored
			//   name so the loader can still match the entity; "rename" carries the
			//   name the user gave it.
			if (!jw.contains("entities")) {
				jw["entities"] = json::array();
			}
			//Drop stale "rename" keys left by earlier saves whose rename no longer
			//holds (e.g. renamed back to the authored name since).
			for (auto& entry : jw["entities"]) {
				if (entry.contains("rename") && entry.contains("name") &&
					state.renamed_entities.find(entry["name"].get<std::string>()) == state.renamed_entities.end()) {
					entry.erase("rename");
				}
			}
			//Every current name that needs an entry: transform-overridden or renamed,
			//excluding clones, created entities and parked ones.
			std::set<std::string> entity_entries;
			for (const auto& name : state.overridden_entities) {
				if (!has_own_record(name)) {
					entity_entries.insert(name);
				}
			}
			//Component add/remove is on its own an edit worth an entry, even for an
			//entity whose transform was never touched.
			for (const auto& [name, delta] : state.component_deltas) {
				if (!has_own_record(name) && c->GetEntityByName(name) != INVALID_ENTITY_ID) {
					entity_entries.insert(name);
				}
			}
			for (const auto& [name, blocks] : state.opaque_components) {
				if (!has_own_record(name)) {
					entity_entries.insert(name);
				}
			}
			for (const auto& [authored, current] : state.renamed_entities) {
				if (!has_own_record(current)) {
					entity_entries.insert(current);
				}
			}
			for (const std::string& current : entity_entries) {
				Entity e = c->GetEntityByName(current);
				if (e == INVALID_ENTITY_ID) {
					continue;
				}
				auto ait = authored_of.find(current);
				std::string authored = (ait != authored_of.end()) ? ait->second : current;

				json* target = nullptr;
				for (auto& entry : jw["entities"]) {
					if (entry.contains("name") && entry["name"] == authored) {
						target = &entry;
						break;
					}
				}
				if (target == nullptr) {
					json new_entry;
					new_entry["name"] = authored;
					jw["entities"].push_back(new_entry);
					target = &jw["entities"].back();
				}
				if (authored != current) {
					(*target)["rename"] = current;
				}
				if (state.overridden_entities.count(current) != 0 && c->ContainsComponent<Transform>(e)) {
					const Transform& t = c->GetComponent<Transform>(e);
					json& transform = (*target)["components"][Transform::NAME];
					transform["position"] = Float3ToJson(t.position);
					transform["scale"] = Float3ToJson(t.scale);
					transform["rotation"] = Float4ToJson(t.rotation);
				}
				WriteComponentDelta(state, c, current, *target);
			}

			//2) Editor-placed instances: fully replace the "instances" array with the
			//   session's current bookkeeping, refreshed from live Transform data for
			//   entities that were also moved after being placed.
			json instances = json::array();
			for (const auto& inst : state.placed_instances) {
				json entry;
				entry["name"] = inst.name;
				entry["template"] = inst.template_name;
				entry["position"] = Float3ToJson(inst.position);
				entry["rotation"] = Float4ToJson(inst.rotation);
				entry["scale"] = Float3ToJson(inst.scale);
				if (!inst.material_name.empty()) {
					entry["material"] = inst.material_name;
				}
				//Per-instance component overrides. Two instances of the same template
				//each carry their own, which is what keeps them independent.
				WriteComponentDelta(state, c, inst.name, entry);
				instances.push_back(entry);
			}
			jw["instances"] = instances;

			//2b) Editor-created copies (clones of scene entities), fully replaced from
			//    the session bookkeeping in creation order so a clone's source always
			//    precedes it. Live transform is read back so gizmo moves are captured.
			json clones = json::array();
			for (const auto& rec : state.cloned_entities) {
				Entity e = c->GetEntityByName(rec.name);
				if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e)) {
					continue;
				}
				std::string source = ResolvePersistentSource(state, rec.source);
				if (source.empty()) {
					//Source can't be reconstructed on load (a cut copy-of-a-copy); skip
					//rather than emit a dangling reference.
					continue;
				}
				const Transform& t = c->GetComponent<Transform>(e);
				json entry;
				entry["name"] = rec.name;
				entry["source"] = source;
				entry["position"] = Float3ToJson(t.position);
				entry["rotation"] = Float4ToJson(t.rotation);
				entry["scale"] = Float3ToJson(t.scale);
				WriteComponentDelta(state, c, rec.name, entry);
				clones.push_back(entry);
			}
			jw["clones"] = clones;

			//2b') Entities created empty in the editor (Add/Entity) and built up
			//     component by component. Written whole from the session bookkeeping, in
			//     creation order, like the two arrays above: nothing else in the file
			//     implies one of these exists, so its record has to carry everything -
			//     the name, the live transform and every component block - and a created
			//     entity that was deleted simply stops being listed.
			json created = json::array();
			for (const auto& name : state.created_entities) {
				Entity e = c->GetEntityByName(name);
				if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e)) {
					continue;
				}
				const Transform& t = c->GetComponent<Transform>(e);
				json entry;
				entry["name"] = name;
				entry["position"] = Float3ToJson(t.position);
				entry["rotation"] = Float4ToJson(t.rotation);
				entry["scale"] = Float3ToJson(t.scale);
				WriteComponentDelta(state, c, name, entry);
				created.push_back(entry);
			}
			jw["created_entities"] = created;

			//2c) Entities the user cut (deleted), by authored name; the loader removes
			//    them after applying renames and clones.
			json removed = json::array();
			for (const auto& name : state.removed_entities) {
				removed.push_back(name);
			}
			jw["removed_entities"] = removed;

			//3) The asset files a future load has to pull in, and the templates built
			//   out of them. Two arrays for two layers: "models" holds the .fbx
			//   imports (meshes, materials, animation clips) and "templates" holds the
			//   objects. A level written before the split has its .fbx entries under
			//   "templates"; they are moved here, which is the whole of the migration.
			if (!jw.contains("templates")) {
				jw["templates"] = json::array();
			}
			if (!jw.contains("models")) {
				jw["models"] = json::array();
			}
			auto list_template = [&jw](const std::string& reference) {
				for (auto& entry : jw["templates"]) {
					if (entry.contains("file") && entry["file"] == reference) {
						return;
					}
				}
				json entry;
				entry["file"] = reference;
				jw["templates"].push_back(entry);
			};
			//The reference is made relative to the *world's* assets path, because that
			//is what World::Load resolves it against - which is not always "<project
			//root>/Assets" (a hand-written level may point "path" at the project root).
			auto model_reference = [&state](const std::string& file_path) {
				std::error_code ec;
				fs::path rel = fs::relative(file_path, fs::path(state.world->GetAssetsPath()), ec);
				return (ec || rel.empty())
					? (std::string("Objects\\") + fs::path(file_path).filename().string())
					: rel.string();
				};
			auto list_model = [&jw](const std::string& reference, bool triangulate,
				const std::string& name) {
				//The name is written only when it is not the file stem, which is what
				//World::LoadModel falls back to: every level that never renamed a model
				//keeps the two-key entry it has always had.
				const bool named =
					name != fs::path(reference).filename().replace_extension().string();
				for (auto& entry : jw["models"]) {
					if (entry.contains("file") && entry["file"] == reference) {
						if (named) {
							entry["name"] = name;
						}
						else {
							entry.erase("name");
						}
						return;
					}
				}
				json entry;
				entry["file"] = reference;
				entry["triangulate"] = triangulate;
				if (named) {
					entry["name"] = name;
				}
				jw["models"].push_back(entry);
			};
			//An inline template's definition lives here rather than in a file, so its
			//entry is rewritten from the live definition every save - unlike a file
			//reference, which is just a name and never changes.
			auto write_inline_template = [&jw, &state](const std::string& name) {
				const json* components = state.world->GetTemplateComponents(name);
				if (components == nullptr) {
					return;
				}
				json* target = nullptr;
				for (auto& entry : jw["templates"]) {
					if (entry.contains("name") && entry["name"] == name && entry.contains("components")) {
						target = &entry;
						break;
					}
				}
				if (target == nullptr) {
					jw["templates"].push_back(json::object());
					target = &jw["templates"].back();
				}
				(*target)["name"] = name;
				(*target)["components"] = *components;
				const json* parts = state.world->GetTemplateParts(name);
				if (parts != nullptr && !parts->empty()) {
					(*target)["parts"] = *parts;
				}
				else {
					//A template that stopped being composed must stop carrying the parts
					//of the version that was.
					target->erase("parts");
				}
			};
			//Templates are always listed: they are this project's own content, and one
			//with no instances yet is still worth keeping.
			for (const auto& t : state.templates) {
				if (TemplateOps::IsInline(state, t.name)) {
					write_inline_template(t.name);
				}
				else {
					list_template(TemplateOps::TemplateReference(t.name));
				}
			}

			//Which models this level actually needs. The Assets/Objects scan finds
			//every file in the folder, and listing all of them would make every level
			//load every asset the project owns - so a model earns its entry by being
			//named: by a template's mesh, material or animation clips, or by an
			//instance placed straight off it (a pre-split level - see
			//World::GetTemplateEntities).
			std::set<std::string> models_in_use;
			auto contains = [](const std::vector<std::string>& names, const std::string& name) {
				return std::find(names.begin(), names.end(), name) != names.end();
				};
			//The model that supplies `asset_name`: a mesh or material by name, or - for
			//an animation clip - the model whose set holds it.
			auto use_model_of = [&](const std::string& asset_name, bool animation_clip) {
				if (asset_name.empty()) {
					return;
				}
				const std::string set = animation_clip
					? state.world->FindAnimationSet(asset_name) : std::string();
				if (animation_clip && set.empty()) {
					return;
				}
				for (const auto& m : state.models) {
					const World::ModelAssets* assets = state.world->GetModelAssets(m.name);
					if (assets == nullptr) {
						continue;
					}
					const bool supplies = animation_clip
						? contains(assets->animation_sets, set)
						: (contains(assets->meshes, asset_name) ||
						   contains(assets->materials, asset_name));
					if (supplies) {
						models_in_use.insert(m.name);
						return;
					}
				}
				};
			//Credits whatever model supplies a "components" block's Mesh/Material,
			//exactly as a template's own components do below - shared so a mesh
			//assigned straight onto a scene/created entity (never a template) earns
			//its model's entry too, instead of being silently dropped from "models"
			//while the entity's own Mesh block still names it.
			auto credit_components = [&](const json& components) {
				if (components.contains(Mesh::NAME)) {
					const json& mesh = components[Mesh::NAME];
					use_model_of(mesh.value("name", std::string()), false);
					if (mesh.contains("clips") && mesh["clips"].is_object()) {
						for (auto it = mesh["clips"].begin(); it != mesh["clips"].end(); ++it) {
							if (it.value().is_string()) {
								use_model_of(it.value().get<std::string>(), true);
							}
						}
					}
				}
				if (components.contains(Material::NAME)) {
					use_model_of(components[Material::NAME].value("name", std::string()), false);
				}
				};
			for (const auto& t : state.templates) {
				const json* components = state.world->GetTemplateComponents(t.name);
				if (components != nullptr) {
					credit_components(*components);
				}
			}
			for (const auto& inst : state.placed_instances) {
				if (state.world->IsModelLoaded(inst.template_name)) {
					models_in_use.insert(inst.template_name);
				}
			}
			//Entities that get their own record (overridden/renamed scene entities,
			//placed-instance overrides, clones and Add/Entity-created ones) may carry
			//a Mesh/Material assigned directly rather than inherited from a template.
			for (const json* array : { &jw["entities"], &instances, &clones, &created }) {
				for (const auto& entry : *array) {
					if (entry.contains("components")) {
						credit_components(entry["components"]);
					}
				}
			}
			//Plus whatever the level already listed: a level that loads a model for a
			//reason the editor cannot see (a game attaches its clips in code, an .fbx
			//supplies the sky's geometry) must not have it dropped from under it.
			for (const auto& m : state.models) {
				const World::ModelAssets* assets = state.world->GetModelAssets(m.name);
				const bool listed_before = assets != nullptr && !assets->file.empty() &&
					!fs::path(assets->file).is_absolute();
				//A model imported under a name of its own always earns its entry, used or
				//not: that name is authoring data and the file is the only other place it
				//could come from - and the file says the stem. Everything else still has
				//to be named by something (see above), or opening a project would mean
				//loading every asset in it.
				const bool named = !m.file_path.empty() &&
					m.name != fs::path(m.file_path).filename().replace_extension().string();
				if (!listed_before && !named && models_in_use.count(m.name) == 0) {
					continue;
				}
				const std::string reference = (assets != nullptr && !assets->file.empty() &&
					!fs::path(assets->file).is_absolute())
					? assets->file : model_reference(m.file_path);
				list_model(reference, assets != nullptr && assets->triangulate, m.name);
			}
			//A model the user removed has to *leave* the array, and this section merges
			//into what the file already had rather than rewriting it - so an entry whose
			//model is no longer in the project would otherwise sit there and be loaded
			//again on the next open. Keyed by the file reference, which is what an entry
			//actually carries; a model still in the project keeps its entry whether or
			//not this save had a reason to write it.
			{
				std::set<std::string> project_models;
				for (const auto& m : state.models) {
					const World::ModelAssets* assets = state.world->GetModelAssets(m.name);
					if (assets != nullptr && !assets->file.empty() &&
						!fs::path(assets->file).is_absolute()) {
						project_models.insert(assets->file);
					}
					if (!m.file_path.empty()) {
						project_models.insert(model_reference(m.file_path));
					}
				}
				json kept = json::array();
				for (auto& entry : jw["models"]) {
					if (!entry.contains("file") || !entry["file"].is_string() ||
						project_models.count(entry["file"].get<std::string>()) != 0) {
						kept.push_back(entry);
					}
				}
				jw["models"] = kept;
			}
			//Every .fbx that used to sit in "templates" now lives in "models", so the
			//old entries go. Their assets are still loaded - by the array above - and
			//instances that named one still resolve through the model registry.
			{
				json kept = json::array();
				for (auto& entry : jw["templates"]) {
					const bool is_fbx = entry.contains("file") && entry["file"].is_string() &&
						fs::path(entry["file"].get<std::string>()).extension() != ".tpl";
					if (!is_fbx) {
						kept.push_back(entry);
					}
				}
				jw["templates"] = kept;
			}
			//A template that stopped being inline (it moved into a .tpl) must lose its
			//stale inline entry, or the next load would register the old definition on
			//top of the file's.
			{
				json kept = json::array();
				for (auto& entry : jw["templates"]) {
					const bool stale_inline = entry.contains("components") && entry.contains("name") &&
						entry["name"].is_string() &&
						!TemplateOps::IsInline(state, entry["name"].get<std::string>());
					if (!stale_inline) {
						kept.push_back(entry);
					}
				}
				jw["templates"] = kept;
			}
			for (const std::string& gone : removed_templates) {
				const std::string reference = TemplateOps::TemplateReference(gone);
				json kept = json::array();
				for (auto& entry : jw["templates"]) {
					if (!entry.contains("file") || entry["file"] != reference) {
						kept.push_back(entry);
					}
				}
				jw["templates"] = kept;
			}

				//3b) Meshes this level had generated rather than imported - the levels of
				//    detail simplified out of a model (World::GenerateMeshLod). The
				//    recipe is what is written, not just the file it was cached into: a
				//    cache can be missing or stale and a recipe can always be run again,
				//    which is what makes the folder of .hbmesh files safe to delete.
				//
				//    Written whole from the world's registry rather than merged entry by
				//    entry: the registry is everything this session loaded plus
				//    everything it generated, so it already contains what the file had.
				{
					json generated = json::array();
					for (const World::GeneratedMesh& g : state.world->GetGeneratedMeshes()) {
						generated.push_back(json{ {"name", g.name}, {"source", g.source},
												  {"ratio", g.ratio}, {"file", g.file} });
					}
					if (generated.empty()) {
						jw.erase("generated_meshes");
					}
					else {
						jw["generated_meshes"] = generated;
					}
				}

				//4) Entity groups (the Entities panel tree). Stored under a top-level
			//   "editor" object that World::Load never reads, so it round-trips as
			//   editor-only data. Assignments to entities that no longer exist are
			//   dropped here rather than accumulating in the file.
			json groups = json::object();
			for (const auto& g : state.entity_groups) {
				groups[g] = json::array();
			}
			for (const auto& [entity_name, group] : state.entity_group_of) {
				if (groups.contains(group) && !EntityOps::IsParkedName(entity_name) &&
					c->GetEntityByName(entity_name) != INVALID_ENTITY_ID) {
					groups[group].push_back(entity_name);
				}
			}
			level["editor"]["groups"] = groups;

			//5) Grid snapping (SceneEditor.h's grid_snap_enabled/grid_size/
			//   grid_rotation_step_degrees/grid_scale_step/grid_lines_visible) -
			//   editor-only view state, same "editor" block as the groups above.
			level["editor"]["grid"] = {
				{ "enabled", state.grid_snap_enabled },
				{ "size", state.grid_size },
				{ "rotation_step_degrees", state.grid_rotation_step_degrees },
				{ "scale_step", state.grid_scale_step },
				{ "lines_visible", state.grid_lines_visible },
			};

			//6) Render settings (RenderSettings::ToJson's snapshot, taken by the
			//   File/Save Level handler right before calling here - the same
			//   "editor" block as the grid and groups above, since these are the
			//   session's dialed-in toggles rather than anything the engine loader
			//   reads). Erased rather than written empty, so a level saved before a
			//   level was ever loaded (there is none) or with nothing worth storing
			//   does not gain a stray empty block.
			if (state.render_settings.is_object() && !state.render_settings.empty()) {
				level["editor"]["render"] = state.render_settings;
			}
			else {
				level["editor"].erase("render");
			}

			std::ofstream out(state.current_level_path);
			out << level.dump(4);
			out.close();

			state.status_message = "Saved: " + state.current_level_path;
			//Only on the success path, after materials/templates/level are all written -
			//this is the one place HasUnsavedChanges' three sources all get cleared.
			EditorHistory::MarkSaved();
		}

		//The inverse of Float3ToJson/Float4ToJson, for re-deriving instance records.
		static float3 JsonToFloat3(const json& j, const char* key, const float3& fallback)
		{
			if (!j.contains(key) || !j[key].is_object()) {
				return fallback;
			}
			const json& v = j[key];
			return { v.value("x", fallback.x), v.value("y", fallback.y), v.value("z", fallback.z) };
		}

		static float4 JsonToFloat4(const json& j, const char* key, const float4& fallback)
		{
			if (!j.contains(key) || !j[key].is_object()) {
				return fallback;
			}
			const json& v = j[key];
			return { v.value("x", fallback.x), v.value("y", fallback.y),
				v.value("z", fallback.z), v.value("w", fallback.w) };
		}

		void LoadEditorData(EditorState& state, const std::string& level_json_path)
		{
			state.entity_groups.clear();
			state.entity_group_of.clear();
			//Grid snapping is per-level view state (like the groups above); reset to
			//the EditorState defaults so a level with no "grid" block - or none at
			//all, on File/New - doesn't inherit whatever a previously open level had.
			state.grid_snap_enabled = false;
			state.grid_size = 1.0f;
			state.grid_rotation_step_degrees = 15.0f;
			state.grid_scale_step = 0.1f;
			state.grid_lines_visible = false;
			//Render settings, same reasoning: reset here so a level with none stored
			//applies RenderSettings::ApplyHighDefaults untouched rather than carrying
			//over whatever a previously open level in this session had. Repopulated
			//below if the file has an "editor"/"render" block; actually applied later,
			//in SceneEditorApp::OpenLevel right after ApplyHighDefaults, once the DOF
			//effect it may need exists.
			state.render_settings = json::object();
			state.renamed_entities.clear();
			state.cloned_entities.clear();
			state.created_entities.clear();
			state.removed_entities.clear();
			state.parked_entities.clear();
			state.component_deltas.clear();
			state.opaque_components.clear();
			state.placed_instances.clear();
			state.instance_entity_ids.clear();
			state.dirty_templates.clear();
			state.removed_templates.clear();
			state.inline_templates.clear();
			state.clipboard = {};

			json level;
			try {
				level = json::parse(std::ifstream(level_json_path));
			}
			catch (std::exception&) {
				return;
			}

			//Re-derive the copy/rename/delete bookkeeping from what World::Load just
			//applied to the scene, so a save that follows preserves it rather than
			//dropping the previously persisted edits. These live under "world"
			//alongside the data the engine loader reads.
			if (level.contains("world")) {
				const json& jw = level["world"];
				if (jw.contains("entities")) {
					for (const auto& entry : jw["entities"]) {
						if (entry.contains("name") && entry.contains("rename")) {
							std::string authored = entry["name"];
							std::string current = entry["rename"];
							if (!authored.empty() && !current.empty() && authored != current) {
								state.renamed_entities[authored] = current;
							}
						}
					}
				}
				if (jw.contains("clones")) {
					for (const auto& entry : jw["clones"]) {
						if (entry.contains("name") && entry.contains("source")) {
							state.cloned_entities.push_back({ entry["name"], entry["source"] });
						}
					}
				}

				//Entities the level created from nothing. Re-derived for the same reason
				//the instance records are: Save rewrites "created_entities" wholesale from
				//this list, so starting empty would delete every hand-built entity in the
				//level the first time it was saved. A record whose entity did not make it
				//into the scene is dropped rather than re-emitted.
				if (jw.contains("created_entities") && jw["created_entities"].is_array()) {
					Coordinator* c = state.world->GetCoordinator();
					for (const auto& entry : jw["created_entities"]) {
						if (!entry.contains("name") || !entry["name"].is_string()) {
							continue;
						}
						const std::string name = entry["name"];
						if (c != nullptr && c->GetEntityByName(name) != INVALID_ENTITY_ID) {
							state.created_entities.push_back(name);
						}
					}
				}

				//Which templates this level stores inline rather than referencing by
				//file. Re-derived so a save writes each one back the way it came in;
				//without it every inline template would silently migrate into a .tpl.
				if (jw.contains("templates") && jw["templates"].is_array()) {
					for (const auto& entry : jw["templates"]) {
						if (entry.contains("components") && entry.contains("name") &&
							entry["name"].is_string()) {
							state.inline_templates.insert(entry["name"].get<std::string>());
						}
					}
				}

				//Editor-placed instances, re-derived from the records World::LoadInstances
				//has just spawned from. Without this the bookkeeping would start empty
				//while the entities are in the scene, and Save - which rewrites
				//"instances" wholesale from it - would silently delete every object the
				//level had placed. It also puts each instance's entities back in
				//`instance_entity_ids`, which is what makes a later move update the
				//instance record instead of writing a bogus "entities" override for a
				//name that only exists as an instance.
				if (jw.contains("instances") && jw["instances"].is_array()) {
					Coordinator* c = state.world->GetCoordinator();
					for (const auto& entry : jw["instances"]) {
						if (!entry.contains("name") || !entry["name"].is_string() ||
							!entry.contains("template") || !entry["template"].is_string()) {
							continue;
						}
						PlacedInstance inst;
						inst.name = entry["name"];
						inst.template_name = entry["template"];
						inst.material_name = entry.value("material", std::string());
						inst.position = JsonToFloat3(entry, "position", inst.position);
						inst.rotation = JsonToFloat4(entry, "rotation", inst.rotation);
						inst.scale = JsonToFloat3(entry, "scale", inst.scale);
						//A record whose template is gone spawned nothing; keeping it would
						//re-emit a reference the next load cannot resolve either.
						if (c == nullptr ||
							!state.world->IsTemplateLoaded(inst.template_name)) {
							continue;
						}
						for (const std::string& part : AssetBrowser::InstancePartNames(
							state, inst.name, inst.template_name)) {
							Entity e = c->GetEntityByName(part);
							if (e != INVALID_ENTITY_ID) {
								state.instance_entity_ids.insert(e);
							}
						}
						state.placed_instances.push_back(inst);
					}
				}
				if (jw.contains("removed_entities")) {
					for (const auto& name : jw["removed_entities"]) {
						if (name.is_string()) {
							state.removed_entities.insert(name.get<std::string>());
						}
					}
				}

				//Component blocks already in the file, re-derived so a save that follows
				//preserves them instead of dropping them.
				//
				//The split matters: a block this binary has a registered component for
				//was applied to the live entity by World::Load, so it will be
				//re-serialized from there and needs no bookkeeping. A block it does NOT
				//recognize - any component belonging to the game rather than the engine -
				//was skipped by the loader and exists nowhere but the file, so it is held
				//here verbatim. Without that, opening a game's level in the editor and
				//saving would quietly delete every one of its own components.
				for (const char* section : { "entities", "instances", "clones", "created_entities" }) {
					if (!jw.contains(section) || !jw[section].is_array()) {
						continue;
					}
					for (const auto& record : jw[section]) {
						if (!record.contains("name") || !record["name"].is_string() ||
							!record.contains("components") || !record["components"].is_object()) {
							continue;
						}
						const std::string name = record["name"];
						//The "instances" and "created_entities" records are the exception to
						//"needs no bookkeeping" above: Save rewrites those arrays wholesale
						//from its own lists, so a block not in the deltas is not merged - it
						//is dropped. The delta re-serializes from the live entity, so what
						//gets written back is that entity's actual state, override included.
						const bool rewritten_wholesale = (std::string(section) == "instances" ||
							std::string(section) == "created_entities");
						for (const auto& [component, value] : record["components"].items()) {
							if (ECS::ComponentRegistry::Instance().Find(component) == nullptr) {
								state.opaque_components[name][component] = value;
							}
							else if (rewritten_wholesale) {
								state.component_deltas[name].added[component] = value;
							}
						}
					}
					//"remove" lists are re-derived too, so an entity whose component was
					//stripped in a previous session keeps it stripped on the next save.
					for (const auto& record : jw[section]) {
						if (!record.contains("name") || !record["name"].is_string() ||
							!record.contains("remove") || !record["remove"].is_array()) {
							continue;
						}
						const std::string name = record["name"];
						for (const auto& component : record["remove"]) {
							if (component.is_string()) {
								state.component_deltas[name].removed.insert(component.get<std::string>());
							}
						}
					}
				}
			}

			if (!level.contains("editor")) {
				return;
			}
			const json& editor = level["editor"];

			if (editor.contains("groups") && editor["groups"].is_object()) {
				for (const auto& [group, members] : editor["groups"].items()) {
					state.entity_groups.insert(group);
					if (!members.is_array()) {
						continue;
					}
					for (const auto& m : members) {
						if (m.is_string()) {
							state.entity_group_of[m.get<std::string>()] = group;
						}
					}
				}
			}

			//Grid snapping - defaults (see EditorState) apply as-is to a level saved
			//before this existed, so every key is read with value().
			if (editor.contains("grid") && editor["grid"].is_object()) {
				const json& grid = editor["grid"];
				state.grid_snap_enabled = grid.value("enabled", state.grid_snap_enabled);
				state.grid_size = grid.value("size", state.grid_size);
				state.grid_rotation_step_degrees = grid.value("rotation_step_degrees", state.grid_rotation_step_degrees);
				state.grid_scale_step = grid.value("scale_step", state.grid_scale_step);
				state.grid_lines_visible = grid.value("lines_visible", state.grid_lines_visible);
			}

			//Render settings - just carried as-is; RenderSettings::ApplyFromJson (called
			//from SceneEditorApp::OpenLevel, after this and after ApplyHighDefaults) is
			//what actually applies each field, since it needs the DOF effect that does
			//not exist yet at this point in the load.
			if (editor.contains("render") && editor["render"].is_object()) {
				state.render_settings = editor["render"];
			}
		}

	}
}
