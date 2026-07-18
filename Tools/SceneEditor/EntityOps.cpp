#include "EntityOps.h"
#include "EditorHistory.h"
#include "Inspector.h"
#include "AssetBrowser.h"

#include <Components/Base.h>
#include <Components/Physics.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace EntityOps {

		static constexpr const char* PARKED_PREFIX = "__cut_";

		bool IsParkedName(const std::string& name)
		{
			return name.rfind(PARKED_PREFIX, 0) == 0;
		}

		//The placed-instance record owning `entity_name`: an exact match, or the
		//"<record name>_<digits>" pattern SpawnInstance uses for multi-part parts.
		static PlacedInstance* FindInstanceRecord(EditorState& state, const std::string& entity_name)
		{
			for (auto& rec : state.placed_instances) {
				if (rec.name == entity_name) {
					return &rec;
				}
			}
			for (auto& rec : state.placed_instances) {
				if (entity_name.size() > rec.name.size() + 1 &&
					entity_name.compare(0, rec.name.size(), rec.name) == 0 &&
					entity_name[rec.name.size()] == '_') {
					size_t digits = rec.name.size() + 1;
					while (digits < entity_name.size() && isdigit((unsigned char)entity_name[digits])) {
						++digits;
					}
					if (digits == entity_name.size()) {
						return &rec;
					}
				}
			}
			return nullptr;
		}

		//Renames the entity and every piece of editor state keyed by its name,
		//*except* the instance records and the authored-rename map (the callers
		//decide those: a park must not look like a user rename). The clone records'
		//`source` fields follow too, so a record always points at the current name
		//of its source entity.
		static void RenameEverywhere(EditorState& state, const std::string& old_name, const std::string& new_name)
		{
			Coordinator* c = state.world->GetCoordinator();
			c->ChangeEntityName(old_name, new_name);
			Entity e = c->GetEntityByName(new_name);
			if (e != INVALID_ENTITY_ID && c->ContainsComponent<Base>(e)) {
				c->GetComponent<Base>(e).name = new_name;
			}
			for (auto& rec : state.cloned_entities) {
				if (rec.name == old_name) {
					rec.name = new_name;
				}
				if (rec.source == old_name) {
					rec.source = new_name;
				}
			}
			if (state.overridden_entities.erase(old_name) != 0) {
				state.overridden_entities.insert(new_name);
			}
			auto git = state.entity_group_of.find(old_name);
			if (git != state.entity_group_of.end()) {
				std::string group = git->second;
				state.entity_group_of.erase(git);
				state.entity_group_of[new_name] = group;
			}
			if (state.clipboard.kind == EntityClipboard::Kind::SceneEntity &&
				state.clipboard.source_name == old_name) {
				state.clipboard.source_name = new_name;
			}
		}

		static bool IsCloneRecordName(EditorState& state, const std::string& name)
		{
			for (const auto& rec : state.cloned_entities) {
				if (rec.name == name) {
					return true;
				}
			}
			return false;
		}

		//The authored (load-time) name behind `current_name`, i.e. the key the
		//serializer must use in the level's "entities"/"removed_entities" sections.
		static std::string AuthoredNameOf(EditorState& state, const std::string& current_name)
		{
			for (const auto& [authored, current] : state.renamed_entities) {
				if (current == current_name) {
					return authored;
				}
			}
			return current_name;
		}

		bool RenameEntity(EditorState& state, const std::string& old_name,
			const std::string& new_name, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || c->GetEntityByName(old_name) == INVALID_ENTITY_ID) {
				error = "entity not found: " + old_name;
				return false;
			}
			if (new_name.empty()) {
				error = "new name is empty";
				return false;
			}
			if (new_name == old_name) {
				return true; //no-op, nothing to record
			}
			if (IsParkedName(old_name) || IsParkedName(new_name)) {
				error = "reserved name";
				return false;
			}
			if (new_name.find_first_of("*\"") != std::string::npos) {
				error = "name must not contain * or \"";
				return false;
			}
			if (c->GetEntityByName(new_name) != INVALID_ENTITY_ID) {
				error = "an entity named '" + new_name + "' already exists";
				return false;
			}

			PlacedInstance* rec = (state.instance_entity_ids.count(c->GetEntityByName(old_name)) != 0)
				? FindInstanceRecord(state, old_name) : nullptr;
			if (rec != nullptr && rec->name != old_name) {
				error = "'" + old_name + "' is one part of instance '" + rec->name +
					"'; parts cannot be renamed individually";
				return false;
			}

			if (rec != nullptr) {
				rec->name = new_name;
			}
			else if (!IsCloneRecordName(state, old_name)) {
				//An authored entity (FBX scene or JSON lights): persist the rename
				//through the authored-name map. Chained renames keep the original key.
				std::string authored = AuthoredNameOf(state, old_name);
				if (authored == new_name) {
					state.renamed_entities.erase(authored); //renamed back to authored name
				}
				else {
					state.renamed_entities[authored] = new_name;
				}
			}
			//Clones need nothing extra: RenameEverywhere updates their record.
			RenameEverywhere(state, old_name, new_name);
			state.status_message = "Renamed: " + old_name + " -> " + new_name;

			EditorHistory::Push({
				"rename " + old_name + " -> " + new_name,
				[old_name, new_name](EditorState& s) {
					std::string err;
					RenameEntity(s, new_name, old_name, err);
				},
				[old_name, new_name](EditorState& s) {
					std::string err;
					RenameEntity(s, old_name, new_name, err);
				} });
			return true;
		}

		//First free "<base>_copy", "<base>_copy2", ... name. The "_0" probe covers
		//multi-part instances, whose parts claim "<name>_<index>".
		static std::string MakeCopyName(EditorState& state, const std::string& base)
		{
			Coordinator* c = state.world->GetCoordinator();
			for (int n = 1;; ++n) {
				std::string candidate = base + "_copy" + (n > 1 ? std::to_string(n) : "");
				if (c->GetEntityByName(candidate) == INVALID_ENTITY_ID &&
					c->GetEntityByName(candidate + "_0") == INVALID_ENTITY_ID) {
					return candidate;
				}
			}
		}

		bool CanCopySelected(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID ||
				!c->ContainsComponent<Base>(state.selected_entity)) {
				return false;
			}
			if (state.instance_entity_ids.count(state.selected_entity) != 0) {
				return true;
			}
			return c->ContainsComponent<Transform>(state.selected_entity) &&
				c->ContainsComponent<Bounds>(state.selected_entity) &&
				c->ContainsComponent<Mesh>(state.selected_entity);
		}

		bool CopySelected(EditorState& state, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID ||
				!c->ContainsComponent<Base>(state.selected_entity)) {
				error = "no entity selected";
				return false;
			}
			const Base& base = c->GetComponent<Base>(state.selected_entity);

			if (state.instance_entity_ids.count(state.selected_entity) != 0) {
				PlacedInstance* rec = FindInstanceRecord(state, base.name);
				if (rec == nullptr) {
					error = "no instance record for entity: " + base.name;
					return false;
				}
				state.clipboard = {};
				state.clipboard.kind = EntityClipboard::Kind::Instance;
				state.clipboard.instance = *rec;
				state.status_message = "Copied: " + rec->name;
				return true;
			}

			if (!c->ContainsComponent<Transform>(state.selected_entity) ||
				!c->ContainsComponent<Bounds>(state.selected_entity) ||
				!c->ContainsComponent<Mesh>(state.selected_entity)) {
				error = "only mesh entities and placed instances can be copied";
				return false;
			}
			const Transform& t = c->GetComponent<Transform>(state.selected_entity);
			state.clipboard = {};
			state.clipboard.kind = EntityClipboard::Kind::SceneEntity;
			state.clipboard.source_name = base.name;
			state.clipboard.display_name = base.name;
			state.clipboard.position = t.position;
			state.clipboard.rotation = t.rotation;
			state.clipboard.scale = t.scale;
			state.clipboard.visible = base.visible;
			state.clipboard.scene_visible = base.scene_visible;
			state.clipboard.cast_shadow = base.cast_shadow;
			state.status_message = "Copied: " + base.name;
			return true;
		}

		//Everything a park/unpark closure needs, captured by value.
		struct ParkInfo {
			std::string name;         // the entity's visible name
			std::string parked_name;  // its name while parked
			std::string authored;     // authored name ("" when the entity is a clone)
			std::string renamed_from; // authored key removed from renamed_entities ("" if none)
			ClonedEntity clone_record;// the removed clone record (clone cuts only)
			size_t clone_record_index = 0;
			bool is_clone = false;
			bool visible = true;
			bool scene_visible = true;
			bool cast_shadow = true;
		};

		//Hides the entity under its parked name and pulls it out of the persisted
		//state. The reverse of UnparkEntity; both are idempotent enough to serve as
		//the redo/undo closures of a cut.
		static void ParkEntity(EditorState& state, const ParkInfo& info)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = c->GetEntityByName(info.name);
			if (e == INVALID_ENTITY_ID) {
				return;
			}
			if (info.is_clone) {
				for (auto it = state.cloned_entities.begin(); it != state.cloned_entities.end(); ++it) {
					if (it->name == info.name) {
						state.cloned_entities.erase(it);
						break;
					}
				}
			}
			else {
				state.removed_entities.insert(info.authored);
				if (!info.renamed_from.empty()) {
					state.renamed_entities.erase(info.renamed_from);
				}
			}
			RenameEverywhere(state, info.name, info.parked_name);
			state.parked_entities[info.parked_name] = info.is_clone ? "" : info.authored;

			Base& base = c->GetComponent<Base>(e);
			base.visible = false;
			base.scene_visible = false;
			base.cast_shadow = false;
			if (c->ContainsComponent<Physics>(e)) {
				std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
				c->GetComponent<Physics>(e).SetEnabled(false);
			}
			if (state.selected_entity == e) {
				state.selected_entity = INVALID_ENTITY_ID;
			}
		}

		static void UnparkEntity(EditorState& state, const ParkInfo& info)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = c->GetEntityByName(info.parked_name);
			if (e == INVALID_ENTITY_ID) {
				return;
			}
			state.parked_entities.erase(info.parked_name);
			RenameEverywhere(state, info.parked_name, info.name);
			if (info.is_clone) {
				//Back at its original position so sources still precede dependents
				//when the "clones" array is saved.
				size_t at = (std::min)(info.clone_record_index, state.cloned_entities.size());
				state.cloned_entities.insert(state.cloned_entities.begin() + at, info.clone_record);
			}
			else {
				state.removed_entities.erase(info.authored);
				if (!info.renamed_from.empty()) {
					state.renamed_entities[info.renamed_from] = info.name;
				}
			}
			Base& base = c->GetComponent<Base>(e);
			base.visible = info.visible;
			base.scene_visible = info.scene_visible;
			base.cast_shadow = info.cast_shadow;
			if (c->ContainsComponent<Physics>(e)) {
				std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
				c->GetComponent<Physics>(e).SetEnabled(true);
			}
			state.selected_entity = e;
			Inspector::RefreshEulerCache(state);
		}

		bool CutSelected(EditorState& state, std::string& error)
		{
			//Copy first: the clipboard must capture the entity's visible state
			//before parking hides it.
			if (!CopySelected(state, error)) {
				return false;
			}
			Coordinator* c = state.world->GetCoordinator();

			if (state.clipboard.kind == EntityClipboard::Kind::Instance) {
				PlacedInstance record = state.clipboard.instance;
				AssetBrowser::RemovePlacedInstance(state, record.name);
				state.status_message = "Cut: " + record.name;
				EditorHistory::Push({
					"cut " + record.name,
					[record](EditorState& s) {
						std::string err;
						AssetBrowser::SpawnRecordedInstance(s, record, err);
					},
					[record](EditorState& s) {
						AssetBrowser::RemovePlacedInstance(s, record.name);
					} });
				return true;
			}

			ParkInfo info;
			info.name = state.clipboard.display_name;
			info.visible = state.clipboard.visible;
			info.scene_visible = state.clipboard.scene_visible;
			info.cast_shadow = state.clipboard.cast_shadow;
			for (int n = 0;; ++n) {
				info.parked_name = PARKED_PREFIX + info.name + (n > 0 ? "_" + std::to_string(n) : "");
				if (c->GetEntityByName(info.parked_name) == INVALID_ENTITY_ID) {
					break;
				}
			}
			for (size_t i = 0; i < state.cloned_entities.size(); ++i) {
				if (state.cloned_entities[i].name == info.name) {
					info.is_clone = true;
					info.clone_record = state.cloned_entities[i];
					info.clone_record_index = i;
					break;
				}
			}
			if (!info.is_clone) {
				info.authored = AuthoredNameOf(state, info.name);
				if (info.authored != info.name) {
					info.renamed_from = info.authored;
				}
			}

			ParkEntity(state, info);
			state.status_message = "Cut: " + info.name;
			EditorHistory::Push({
				"cut " + info.name,
				[info](EditorState& s) {
					UnparkEntity(s, info);
				},
				[info](EditorState& s) {
					ParkEntity(s, info);
				} });
			return true;
		}

		//Creates the clone described by `clip` under `new_name`, with the full
		//bookkeeping (record, selection, transform). Shared by Paste and the redo
		//closure of a paste, so redoing runs exactly the original paste code.
		static bool SpawnClone(EditorState& state, const EntityClipboard& clip,
			const std::string& new_name, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c->GetEntityByName(clip.source_name) == INVALID_ENTITY_ID) {
				error = "copy source no longer exists: " + clip.display_name;
				return false;
			}
			Entity e = state.world->CloneEntity(new_name, clip.source_name);
			if (e == INVALID_ENTITY_ID) {
				error = "clone failed for: " + clip.display_name;
				return false;
			}
			state.cloned_entities.push_back({ new_name, clip.source_name });
			//A cut source is parked hidden; the paste must come out with the
			//display state captured at copy time, not the parked flags.
			Base& base = c->GetComponent<Base>(e);
			base.visible = clip.visible;
			base.scene_visible = clip.scene_visible;
			base.cast_shadow = clip.cast_shadow;
			Inspector::TransformSnapshot snapshot{ clip.position, clip.rotation, clip.scale };
			std::string err;
			Inspector::ApplySnapshot(state, new_name, snapshot, err);
			state.selected_entity = e;
			Inspector::RefreshEulerCache(state);
			return true;
		}

		//Undo of a paste: destroys the clone and drops its bookkeeping. The clone's
		//physics body (when it has one) dies with its Physics component.
		static void RemoveClone(EditorState& state, const std::string& name)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = c->GetEntityByName(name);
			if (e == INVALID_ENTITY_ID) {
				return;
			}
			for (auto it = state.cloned_entities.begin(); it != state.cloned_entities.end(); ++it) {
				if (it->name == name) {
					state.cloned_entities.erase(it);
					break;
				}
			}
			state.overridden_entities.erase(name);
			if (state.selected_entity == e) {
				state.selected_entity = INVALID_ENTITY_ID;
			}
			c->DestroyEntity(e);
		}

		bool Paste(EditorState& state, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (state.clipboard.kind == EntityClipboard::Kind::None) {
				error = "clipboard is empty";
				return false;
			}

			if (state.clipboard.kind == EntityClipboard::Kind::Instance) {
				PlacedInstance record = state.clipboard.instance;
				record.name = MakeCopyName(state, record.name);
				if (!AssetBrowser::SpawnRecordedInstance(state, record, error)) {
					return false;
				}
				state.status_message = "Pasted: " + record.name;
				EditorHistory::Push({
					"paste " + record.name,
					[record](EditorState& s) {
						AssetBrowser::RemovePlacedInstance(s, record.name);
					},
					[record](EditorState& s) {
						std::string err;
						AssetBrowser::SpawnRecordedInstance(s, record, err);
					} });
				return true;
			}

			EntityClipboard clip = state.clipboard;
			std::string new_name = MakeCopyName(state, clip.display_name);
			if (!SpawnClone(state, clip, new_name, error)) {
				return false;
			}
			state.status_message = "Pasted: " + new_name;
			EditorHistory::Push({
				"paste " + new_name,
				[new_name](EditorState& s) {
					RemoveClone(s, new_name);
				},
				[clip, new_name](EditorState& s) {
					std::string err;
					SpawnClone(s, clip, new_name, err);
				} });
			return true;
		}

	}
}
