#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// Draws the grid-snap spacing as a set of world-space lines on the Y=0 plane
	// around the camera, so a snapped drag has something to visibly align to.
	// Purely a visual aid - projects world points through the active camera into
	// ImGui's background draw list, same as SelectionGizmo/PhysicsDebug, so it
	// needs no engine changes. Gated on EditorState::grid_lines_visible, which is
	// independent of grid_snap_enabled - showing the lines and snapping to them
	// are two separate switches. No-op with the lines hidden.
	namespace GridOverlay {
		void Draw(EditorState& state);
	}
}
