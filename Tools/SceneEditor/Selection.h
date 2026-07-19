#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The editor selection: a set of entities with one of them designated primary
	// (see EditorState::selected_entity). Every surface that changes what is
	// selected - Entities panel rows, viewport clicks, the automation `select`
	// commands, and the entity operations that create or destroy entities - must go
	// through here rather than assigning EditorState::selected_entity, which alone
	// would leave the two fields disagreeing.
	//
	// Selection is deliberately NOT undoable (EditorHistory.h lists it as view
	// state), so nothing in here records history. What *is* undoable is what the
	// selection is then used to do: transform it with the gizmo, regroup it, delete
	// it.
	//
	// Entities are stored as ids, which are recycled when an entity is destroyed
	// and another created. That is safe because the selection is refreshed by the
	// same operations that destroy entities (they call Remove), and because Prune
	// runs each frame to drop anything that died behind the selection's back.
	namespace Selection {

		bool Contains(const EditorState& state, HotBite::Engine::ECS::Entity entity);
		size_t Count(const EditorState& state);

		void Clear(EditorState& state);

		// Replaces the selection with `entity` alone (INVALID_ENTITY_ID clears it),
		// or with `entities` (the last becomes primary).
		void Set(EditorState& state, HotBite::Engine::ECS::Entity entity);
		void Set(EditorState& state, const std::vector<HotBite::Engine::ECS::Entity>& entities);

		// Adds `entity` and makes it primary; a re-added entity moves to primary
		// rather than appearing twice.
		void Add(EditorState& state, HotBite::Engine::ECS::Entity entity);

		// Drops `entity`; the previous member becomes primary when the primary goes.
		void Remove(EditorState& state, HotBite::Engine::ECS::Entity entity);

		// Ctrl+click: removes `entity` when selected, adds it otherwise.
		void Toggle(EditorState& state, HotBite::Engine::ECS::Entity entity);

		// Names of the currently selected entities, primary last. Entities that have
		// gone away are skipped, so this is the safe form to capture in an undo
		// closure (which must address entities by name - ids are recycled).
		std::vector<std::string> Names(EditorState& state);

		// Selecting a group selects its members: every entity currently assigned to
		// `group`, in the Entities panel's alphabetical order. Additive when
		// `additive` (Ctrl+click on the group header). Groups with no live members
		// leave the selection untouched.
		void SelectGroup(EditorState& state, const std::string& group, bool additive);

		// Drops selected entities that no longer exist or have been parked by a cut.
		// Called once per frame before the panels draw, so a selection can never
		// outlive its entities.
		void Prune(EditorState& state);
	}
}
