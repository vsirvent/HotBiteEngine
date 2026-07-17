#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace Outliner {
		void Draw(EditorState& state, EditorCamera& camera);

		// Moves the editor camera to frame the currently selected entity (its world
		// AABB when it has Bounds, its Transform position otherwise). Shared by the
		// Outliner's double-click and the automation `focus` command. Returns false
		// with `error` set when there is no usable selection or camera.
		bool FocusSelected(EditorState& state, EditorCamera& camera, std::string& error);
	}
}
