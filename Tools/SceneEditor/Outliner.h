#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The "Entities" panel: the scene's entity list, ordered alphabetically and
	// arranged as a tree of user-defined groups (drag an entity onto a group header
	// to move it in, onto the panel background to move it out; right-click for the
	// same operations plus group rename/delete).
	namespace Outliner {
		void Draw(EditorState& state, EditorCamera& camera);

		// Moves the editor camera to frame the currently selected entity (its world
		// AABB when it has Bounds, its Transform position otherwise). Shared by the
		// Entities panel's double-click and the automation `focus` command. Returns
		// false with `error` set when there is no usable selection or camera.
		bool FocusSelected(EditorState& state, EditorCamera& camera, std::string& error);

		// Group bookkeeping shared by the panel UI (buttons, drag-and-drop, context
		// menus) and the automation channel (`create_group` / `set_group`). Every
		// successful call records one EditorHistory action, so keep new group
		// mutations going through these instead of touching EditorState directly.
		// SetEntityGroup with an empty group name ungroups; a group named for the
		// first time is created implicitly. RenameGroup onto an existing name merges
		// the two groups.
		bool CreateGroup(EditorState& state, const std::string& name, std::string& error);
		bool SetEntityGroup(EditorState& state, const std::string& entity_name,
			const std::string& group, std::string& error);

		// Moves several entities into (or out of) `group` as ONE undoable action, so
		// regrouping a multi-entity selection undoes in a single step rather than
		// one per entity. Entities already in `group` are skipped. Returns false
		// with `error` set only when nothing could be moved.
		bool SetEntitiesGroup(EditorState& state, const std::vector<std::string>& entity_names,
			const std::string& group, std::string& error);
		bool RenameGroup(EditorState& state, const std::string& from,
			const std::string& to, std::string& error);
		bool DeleteGroup(EditorState& state, const std::string& name, std::string& error);
	}
}
