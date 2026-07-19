#include "Selection.h"
#include "EntityOps.h"
#include "Inspector.h"

#include <Components/Base.h>
#include <algorithm>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Selection {

		//Re-derives the primary from the list and refreshes the Components panel's
		//Euler cache for it. The single place selected_entity is written.
		static void SyncPrimary(EditorState& state)
		{
			Entity primary = state.selected_entities.empty()
				? INVALID_ENTITY_ID : state.selected_entities.back();
			if (primary == state.selected_entity) {
				return;
			}
			state.selected_entity = primary;
			//Euler angles are cached (not recomputed per frame) to keep the rotation
			//fields from jittering near gimbal lock, so a new primary must refresh it.
			Inspector::RefreshEulerCache(state);
		}

		bool Contains(const EditorState& state, Entity entity)
		{
			return std::find(state.selected_entities.begin(), state.selected_entities.end(),
				entity) != state.selected_entities.end();
		}

		size_t Count(const EditorState& state)
		{
			return state.selected_entities.size();
		}

		void Clear(EditorState& state)
		{
			state.selected_entities.clear();
			SyncPrimary(state);
		}

		void Set(EditorState& state, Entity entity)
		{
			state.selected_entities.clear();
			if (entity != INVALID_ENTITY_ID) {
				state.selected_entities.push_back(entity);
			}
			SyncPrimary(state);
		}

		void Set(EditorState& state, const std::vector<Entity>& entities)
		{
			state.selected_entities.clear();
			for (Entity e : entities) {
				if (e != INVALID_ENTITY_ID && !Contains(state, e)) {
					state.selected_entities.push_back(e);
				}
			}
			SyncPrimary(state);
		}

		void Add(EditorState& state, Entity entity)
		{
			if (entity == INVALID_ENTITY_ID) {
				return;
			}
			//Re-adding moves the entity to the end: the most recent pick is always
			//the primary, which is what makes shift-click ranges anchor correctly.
			auto it = std::find(state.selected_entities.begin(), state.selected_entities.end(), entity);
			if (it != state.selected_entities.end()) {
				state.selected_entities.erase(it);
			}
			state.selected_entities.push_back(entity);
			SyncPrimary(state);
		}

		void Remove(EditorState& state, Entity entity)
		{
			auto it = std::find(state.selected_entities.begin(), state.selected_entities.end(), entity);
			if (it != state.selected_entities.end()) {
				state.selected_entities.erase(it);
				SyncPrimary(state);
			}
		}

		void Toggle(EditorState& state, Entity entity)
		{
			if (Contains(state, entity)) {
				Remove(state, entity);
			}
			else {
				Add(state, entity);
			}
		}

		std::vector<std::string> Names(EditorState& state)
		{
			std::vector<std::string> names;
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				return names;
			}
			names.reserve(state.selected_entities.size());
			for (Entity e : state.selected_entities) {
				if (c->ContainsComponent<Base>(e)) {
					names.push_back(c->GetComponent<Base>(e).name);
				}
			}
			return names;
		}

		void SelectGroup(EditorState& state, const std::string& group, bool additive)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				return;
			}
			//Alphabetical, matching how the Entities panel lists the group's rows, so
			//the primary (and therefore the Components panel) is predictable.
			std::vector<std::string> members;
			for (const auto& [entity_name, g] : state.entity_group_of) {
				if (g == group) {
					members.push_back(entity_name);
				}
			}
			std::sort(members.begin(), members.end(), [](const std::string& a, const std::string& b) {
				int cmp = _stricmp(a.c_str(), b.c_str());
				return (cmp != 0) ? cmp < 0 : a < b;
				});

			std::vector<Entity> entities;
			for (const auto& name : members) {
				Entity e = c->GetEntityByName(name);
				if (e != INVALID_ENTITY_ID && !EntityOps::IsParkedName(name)) {
					entities.push_back(e);
				}
			}
			if (entities.empty()) {
				return;
			}
			if (additive) {
				for (Entity e : entities) {
					Add(state, e);
				}
			}
			else {
				Set(state, entities);
			}
		}

		void Prune(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				if (!state.selected_entities.empty()) {
					Clear(state);
				}
				return;
			}
			size_t before = state.selected_entities.size();
			auto dead = [c](Entity e) {
				//A parked entity still exists (a cut keeps it around so paste and undo
				//can reach it) but is hidden from every listing, so it must not stay
				//selected either.
				return !c->ContainsComponent<Base>(e) ||
					EntityOps::IsParkedName(c->GetComponent<Base>(e).name);
			};
			state.selected_entities.erase(
				std::remove_if(state.selected_entities.begin(), state.selected_entities.end(), dead),
				state.selected_entities.end());
			if (state.selected_entities.size() != before) {
				SyncPrimary(state);
			}
		}

	}
}
