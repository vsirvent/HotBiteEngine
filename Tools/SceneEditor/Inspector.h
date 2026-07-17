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

		// Programmatic equivalent of editing the Inspector's DragFloat3 fields on the
		// currently selected entity: applies whichever channels are non-null and runs
		// the same save bookkeeping (placed-instance sync / FBX override tracking).
		// Returns false with `error` set if there is no selected entity with a Transform.
		bool ApplyTransform(EditorState& state,
			const HotBite::Engine::float3* position,
			const HotBite::Engine::float3* scale,
			const HotBite::Engine::float3* euler_degrees,
			std::string& error);
	}
}
