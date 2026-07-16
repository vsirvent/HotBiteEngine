#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace Inspector {
		void Draw(EditorState& state);

		// Recomputes state.inspector_euler_degrees from the currently selected entity's
		// Transform.rotation quaternion. Call on selection change only (not every frame)
		// to avoid visible jitter from repeated quaternion<->Euler round-tripping near
		// gimbal-lock angles.
		void RefreshEulerCache(EditorState& state);
	}
}
