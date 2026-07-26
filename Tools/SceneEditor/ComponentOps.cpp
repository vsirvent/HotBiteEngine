#include "ComponentOps.h"
#include "EditorHistory.h"
#include "EntityOps.h"

#include <World.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;

namespace HotBiteEditor {
	namespace ComponentOps {

		namespace {

			//The registry is process-wide and populated by World::RegisterComponent, so
			//it is reachable without going through the world.
			const ComponentDesc* FindDesc(const std::string& component) {
				return ComponentRegistry::Instance().Find(component);
			}

			//Resolves an entity name to a live entity, refusing parked (cut) entities:
			//they are hidden and inert, and editing their components would write
			//bookkeeping for something that is about to be gone.
			Entity Resolve(EditorState& state, const std::string& entity_name,
				std::string& error)
			{
				Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
				if (c == nullptr) {
					error = "no level loaded";
					return INVALID_ENTITY_ID;
				}
				if (EntityOps::IsParkedName(entity_name)) {
					error = "entity '" + entity_name + "' has been cut";
					return INVALID_ENTITY_ID;
				}
				Entity e = c->GetEntityByName(entity_name);
				if (e == INVALID_ENTITY_ID) {
					error = "unknown entity '" + entity_name + "'";
				}
				return e;
			}

			//Drops an entity's delta entry once it carries nothing, so a component that
			//was removed and then re-added (or vice versa) leaves no trace in the saved
			//file rather than an empty block.
			void PruneDelta(EditorState& state, const std::string& entity_name) {
				auto it = state.component_deltas.find(entity_name);
				if (it != state.component_deltas.end() &&
					it->second.removed.empty() && it->second.added.empty()) {
					state.component_deltas.erase(it);
				}
			}

			SerializeContext MakeContext(EditorState& state) {
				return state.world->MakeSerializeContext();
			}
		}

		bool CanAdd(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& reason)
		{
			Entity e = Resolve(state, entity_name, reason);
			if (e == INVALID_ENTITY_ID) {
				return false;
			}
			const ComponentDesc* desc = FindDesc(component);
			if (desc == nullptr) {
				reason = "unknown component '" + component + "'";
				return false;
			}
			if (!desc->Addable()) {
				reason = component + " cannot be added from the editor";
				return false;
			}
			if (desc->has(state.world->GetCoordinator(), e)) {
				reason = entity_name + " already has " + component;
				return false;
			}
			return true;
		}

		bool CanRemove(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& reason)
		{
			Entity e = Resolve(state, entity_name, reason);
			if (e == INVALID_ENTITY_ID) {
				return false;
			}
			const ComponentDesc* desc = FindDesc(component);
			if (desc == nullptr) {
				reason = "unknown component '" + component + "'";
				return false;
			}
			if (!desc->Removable()) {
				reason = component + (desc->policy == ComponentPolicy::Mandatory
					? " is required by every entity and cannot be removed"
					: " is engine-managed and cannot be removed");
				return false;
			}
			if (!desc->has(state.world->GetCoordinator(), e)) {
				reason = entity_name + " has no " + component;
				return false;
			}
			return true;
		}

		bool AddComponent(EditorState& state, const std::string& entity_name,
			const std::string& component, const nlohmann::json& payload,
			std::string& error)
		{
			if (!CanAdd(state, entity_name, component, error)) {
				return false;
			}
			Entity e = state.world->GetCoordinator()->GetEntityByName(entity_name);
			const ComponentDesc* desc = FindDesc(component);

			desc->apply(MakeContext(state), e, payload);

			//Record the delta so it lands in this entity's record on save, and nowhere
			//else - the whole point of the feature.
			ComponentDelta& delta = state.component_deltas[entity_name];
			delta.removed.erase(component);
			delta.added[component] = payload;

			//Capture the entity NAME, never the id: undo/redo may destroy and re-create
			//entities and ids are recycled (see EditorHistory.h).
			const std::string name = entity_name;
			const std::string comp = component;
			const nlohmann::json captured = payload;
			EditorHistory::Push({
				"add " + comp + " to " + name,
				[name, comp](EditorState& s) {
					std::string ignored;
					RemoveComponent(s, name, comp, ignored);
				},
				[name, comp, captured](EditorState& s) {
					std::string ignored;
					AddComponent(s, name, comp, captured, ignored);
				} });

			state.status_message = "Added " + component + " to " + entity_name;
			return true;
		}

		bool RemoveComponent(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& error)
		{
			if (!CanRemove(state, entity_name, component, error)) {
				return false;
			}
			Entity e = state.world->GetCoordinator()->GetEntityByName(entity_name);
			const ComponentDesc* desc = FindDesc(component);

			//Serialize before removing: undo restores the component with the values it
			//actually had, not a default-constructed one.
			nlohmann::json captured;
			try {
				captured = desc->serialize(MakeContext(state), e);
			}
			catch (const std::exception& ex) {
				//A component that cannot describe itself can still be removed; undo just
				//brings it back with defaults. Better than refusing the edit.
				printf("ComponentOps: could not serialize %s on %s (%s); undo will restore "
					"defaults.\n", component.c_str(), entity_name.c_str(), ex.what());
				captured = nlohmann::json::object();
			}

			desc->remove(MakeContext(state), e);

			ComponentDelta& delta = state.component_deltas[entity_name];
			delta.added.erase(component);
			delta.removed.insert(component);

			const std::string name = entity_name;
			const std::string comp = component;
			EditorHistory::Push({
				"remove " + comp + " from " + name,
				[name, comp, captured](EditorState& s) {
					std::string ignored;
					AddComponent(s, name, comp, captured, ignored);
				},
				[name, comp](EditorState& s) {
					std::string ignored;
					RemoveComponent(s, name, comp, ignored);
				} });

			state.status_message = "Removed " + component + " from " + entity_name;
			return true;
		}

		nlohmann::json GetValue(EditorState& state, const std::string& entity_name,
			const std::string& component)
		{
			std::string error;
			Entity e = Resolve(state, entity_name, error);
			const ComponentDesc* desc = FindDesc(component);
			if (e == INVALID_ENTITY_ID || desc == nullptr ||
				!desc->has(state.world->GetCoordinator(), e)) {
				return nlohmann::json::object();
			}
			try {
				return desc->serialize(MakeContext(state), e);
			}
			catch (const std::exception& ex) {
				printf("ComponentOps: could not serialize %s on %s (%s).\n", component.c_str(),
					entity_name.c_str(), ex.what());
				return nlohmann::json::object();
			}
		}

		//The one place an edited component is written into the entity's delta. It goes
		//in `added` because that is the map SceneSerializer turns into the record's
		//"components" block - which is exactly where an edit belongs, whether the
		//component was added this session or came with the entity.
		static void RecordValueForSave(EditorState& state, const std::string& entity_name,
			const std::string& component)
		{
			ComponentDelta& delta = state.component_deltas[entity_name];
			delta.removed.erase(component);
			delta.added[component] = GetValue(state, entity_name, component);
		}

		void MarkEdited(EditorState& state, const std::string& entity_name,
			const std::string& component)
		{
			std::string error;
			if (Resolve(state, entity_name, error) == INVALID_ENTITY_ID) {
				return;
			}
			RecordValueForSave(state, entity_name, component);
		}

		bool ApplyValue(EditorState& state, const std::string& entity_name,
			const std::string& component, const nlohmann::json& payload, std::string& error)
		{
			Entity e = Resolve(state, entity_name, error);
			if (e == INVALID_ENTITY_ID) {
				return false;
			}
			const ComponentDesc* desc = FindDesc(component);
			if (desc == nullptr) {
				error = "unknown component '" + component + "'";
				return false;
			}
			if (!desc->has(state.world->GetCoordinator(), e)) {
				error = entity_name + " has no " + component;
				return false;
			}
			try {
				desc->apply(MakeContext(state), e, payload);
			}
			catch (const std::exception& ex) {
				error = std::string("could not apply ") + component + ": " + ex.what();
				return false;
			}
			RecordValueForSave(state, entity_name, component);
			return true;
		}

		void RecordEdit(EditorState& state, const std::string& entity_name,
			const std::string& component, const nlohmann::json& before)
		{
			const nlohmann::json after = GetValue(state, entity_name, component);
			if (after == before) {
				return;
			}
			//Entity NAME, never the id, and the payloads by value: an undo may run after
			//the entity has been destroyed and re-created (see EditorHistory.h).
			const std::string name = entity_name;
			const std::string comp = component;
			EditorHistory::Push({
				"edit " + comp + " of " + name,
				[name, comp, before](EditorState& s) {
					std::string ignored;
					ApplyValue(s, name, comp, before, ignored);
				},
				[name, comp, after](EditorState& s) {
					std::string ignored;
					ApplyValue(s, name, comp, after, ignored);
				} });
		}

		bool SetValue(EditorState& state, const std::string& entity_name,
			const std::string& component, const nlohmann::json& payload, std::string& error)
		{
			const nlohmann::json before = GetValue(state, entity_name, component);
			if (!ApplyValue(state, entity_name, component, payload, error)) {
				return false;
			}
			RecordEdit(state, entity_name, component, before);
			state.status_message = "Edited " + component + " of " + entity_name;
			return true;
		}

		std::vector<std::string> ListComponents(EditorState& state,
			const std::string& entity_name)
		{
			std::vector<std::string> names;
			std::string error;
			Entity e = Resolve(state, entity_name, error);
			if (e == INVALID_ENTITY_ID) {
				return names;
			}
			Coordinator* c = state.world->GetCoordinator();
			for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
				if (desc.has(c, e)) {
					names.push_back(desc.name);
				}
			}
			//Components this binary does not define but the level carries anyway (a
			//game's own). They are not on the entity, but they are part of what the
			//entity *is* as far as the file is concerned, so they are listed.
			auto opaque = state.opaque_components.find(entity_name);
			if (opaque != state.opaque_components.end()) {
				for (const auto& [name, value] : opaque->second) {
					names.push_back(name);
				}
			}
			return names;
		}
	}
}
