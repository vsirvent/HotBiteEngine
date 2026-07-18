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

		// Group bookkeeping shared by the panel UI and the automation channel
		// (`create_group` / `set_group`). SetEntityGroup with an empty group name
		// ungroups; a group named for the first time is created implicitly.
		bool CreateGroup(EditorState& state, const std::string& name, std::string& error);
		bool SetEntityGroup(EditorState& state, const std::string& entity_name,
			const std::string& group, std::string& error);
	}
}
