#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace SelectionGizmo {
		// Overlays the selected entity with a translate gizmo (XYZ axis arrows at
		// the rendered world pivot, parent transform included) and, when it has a
		// Bounds component, its world-space AABB. Drawn into ImGui's background
		// draw list (over the 3D scene, under the editor panels) by projecting
		// world points through the active camera, so it needs no engine changes.
		// Left-dragging an axis arrow moves the entity along that axis; the edit
		// goes through Inspector::ApplyTransform so save bookkeeping (instance
		// sync / FBX overrides) matches a manual Inspector edit.
		// Left-clicking the viewport (not on an axis, not over a panel) selects
		// the entity under the cursor: nearest hit against physics colliders and
		// world AABBs (camera-enclosing boxes skipped so terrain-sized volumes
		// don't shadow props), or clears the selection on empty space/sky.
		void Draw(EditorState& state);
	}
}
