#pragma once

#include "SceneEditor.h"
#include <string>

namespace HotBiteEditor {
	// Viewport hints for game components described by a schema (ComponentSchema.h):
	// a field marked "gizmo": "sphere" (a float radius) or "box" (a vec3 of half
	// extents) is drawn around every selected entity carrying that component, so a
	// trigger zone or a spawn radius authored as a number is visible while it is
	// being authored. Drawn like the light gizmos - projected into ImGui's
	// background draw list, no render-pipeline change - and only for the selection,
	// since a level of markers would otherwise be a web of lines.
	namespace SchemaGizmos {

		void Draw(EditorState& state);

		// What the last Draw put on screen, for `schema_gizmo_info`.
		struct FrameInfo {
			int spheres = 0;
			int boxes = 0;
			int segments = 0;
		};
		const FrameInfo& LastFrame();
	}
}
