#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace SelectionGizmo {
		// Overlays the selected entity with its local XYZ axes and, when it has a
		// Bounds component, its world-space AABB. Drawn into ImGui's background
		// draw list (over the 3D scene, under the editor panels) by projecting
		// world points through the active camera, so it needs no engine changes.
		void Draw(EditorState& state);
	}
}
