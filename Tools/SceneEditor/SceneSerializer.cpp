#include "SceneSerializer.h"
#include "EntityOps.h"
#include "PhysicsPreview.h"

#include <Components/Base.h>
#include <Core/Json.h>
#include <ECS/ComponentRegistry.h>
#include <World.h>
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
			//(cut) names never go anywhere except "removed_entities".
			std::set<std::string> clone_names;
			for (const auto& rec : state.cloned_entities) {
				clone_names.insert(rec.name);
			}
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
			//excluding clones and parked entities.
			std::set<std::string> entity_entries;
			for (const auto& name : state.overridden_entities) {
				if (clone_names.count(name) == 0 && !EntityOps::IsParkedName(name)) {
					entity_entries.insert(name);
				}
			}
			//Component add/remove is on its own an edit worth an entry, even for an
			//entity whose transform was never touched.
			for (const auto& [name, delta] : state.component_deltas) {
				if (clone_names.count(name) == 0 && !EntityOps::IsParkedName(name) &&
					c->GetEntityByName(name) != INVALID_ENTITY_ID) {
					entity_entries.insert(name);
				}
			}
			for (const auto& [name, blocks] : state.opaque_components) {
				if (clone_names.count(name) == 0 && !EntityOps::IsParkedName(name)) {
					entity_entries.insert(name);
				}
			}
			for (const auto& [authored, current] : state.renamed_entities) {
				if (clone_names.count(current) == 0 && !EntityOps::IsParkedName(current)) {
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

			//2c) Entities the user cut (deleted), by authored name; the loader removes
			//    them after applying renames and clones.
			json removed = json::array();
			for (const auto& name : state.removed_entities) {
				removed.push_back(name);
			}
			jw["removed_entities"] = removed;

			//3) Newly imported templates: ensure they're listed so a future load pulls
			//   them in automatically (existing/pre-existing ones are already present).
			if (!jw.contains("templates")) {
				jw["templates"] = json::array();
			}
			for (const auto& t : state.templates) {
				if (!t.newly_imported) {
					continue;
				}
				fs::path assets_root = fs::path(state.project_root) / "Assets";
				std::error_code ec;
				fs::path rel = fs::relative(t.file_path, assets_root, ec);
				std::string rel_str = ec ? (std::string("Objects\\") + fs::path(t.file_path).filename().string()) : rel.string();

				bool already_listed = false;
				for (auto& entry : jw["templates"]) {
					if (entry.contains("file") && entry["file"] == rel_str) {
						already_listed = true;
						break;
					}
				}
				if (!already_listed) {
					json entry;
					entry["file"] = rel_str;
					entry["triangulate"] = false;
					jw["templates"].push_back(entry);
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

			std::ofstream out(state.current_level_path);
			out << level.dump(4);
			out.close();

			state.status_message = "Saved: " + state.current_level_path;
		}

		void LoadEditorData(EditorState& state, const std::string& level_json_path)
		{
			state.entity_groups.clear();
			state.entity_group_of.clear();
			state.renamed_entities.clear();
			state.cloned_entities.clear();
			state.removed_entities.clear();
			state.parked_entities.clear();
			state.component_deltas.clear();
			state.opaque_components.clear();
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
				for (const char* section : { "entities", "instances", "clones" }) {
					if (!jw.contains(section) || !jw[section].is_array()) {
						continue;
					}
					for (const auto& record : jw[section]) {
						if (!record.contains("name") || !record["name"].is_string() ||
							!record.contains("components") || !record["components"].is_object()) {
							continue;
						}
						const std::string name = record["name"];
						for (const auto& [component, value] : record["components"].items()) {
							if (ECS::ComponentRegistry::Instance().Find(component) == nullptr) {
								state.opaque_components[name][component] = value;
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

			if (!level.contains("editor") || !level["editor"].contains("groups") ||
				!level["editor"]["groups"].is_object()) {
				return;
			}
			for (const auto& [group, members] : level["editor"]["groups"].items()) {
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

	}
}
