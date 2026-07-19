#pragma once

#include "SceneEditor.h"

#include <ECS/ComponentRegistry.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// Adding and removing components on a selected entity, from every surface that
	// offers it: the Inspector's per-section "x" and Add Component button, and the
	// automation channel's add_component/remove_component.
	//
	// Everything goes through these two helpers rather than calling the registry
	// directly, because each one has three jobs that must not come apart:
	//
	//   1. mutate the live entity through the ECS component registry,
	//   2. record the change in EditorState::component_deltas so SceneSerializer
	//      writes it to *this entity's* record on save - which is what makes the
	//      edit per-entity rather than per-template,
	//   3. push an EditorHistory::Action, per the rule in EditorHistory.h.
	//
	// Both are keyed by entity *name*, never by ECS::Entity: ids are recycled, and
	// an undo that re-creates an entity would otherwise resolve to the wrong one.
	// Removal captures the component's serialized state first, so undo restores it
	// with the values it had rather than a default-constructed one.
	namespace ComponentOps {

		// True when `component` may be added to / removed from `entity_name` right
		// now, with `reason` set when not. Used to grey out the Inspector's buttons
		// and to give the automation channel a real error message.
		bool CanAdd(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& reason);
		bool CanRemove(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& reason);

		// Adds `component` to the entity, initialized from `payload` (pass an empty
		// object for defaults). Fails when the entity is unknown, the component is
		// not registered, it is not addable, or the entity already has it.
		bool AddComponent(EditorState& state, const std::string& entity_name,
			const std::string& component, const nlohmann::json& payload,
			std::string& error);

		// Removes `component` from the entity. Fails when the entity is unknown, the
		// component is not registered or is mandatory, or the entity does not have it.
		bool RemoveComponent(EditorState& state, const std::string& entity_name,
			const std::string& component, std::string& error);

		// The component names currently on `entity_name`, in registry order, plus any
		// the editor is holding for it opaquely (a game component this binary does not
		// define - see EditorState::opaque_components). Used by the Inspector and by
		// the automation channel's `components` command.
		std::vector<std::string> ListComponents(EditorState& state,
			const std::string& entity_name);
	}
}
