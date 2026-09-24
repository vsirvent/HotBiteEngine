#pragma once

#include "SceneEditor.h"
#include <string>
#include <vector>

namespace HotBiteEditor {
	// Viewport gizmos for point lights and spotlights, drawn into ImGui's background
	// draw list like the collider overlay - by projecting world points through the
	// active camera, so no render-pipeline change is involved.
	//
	// Two independent switches, both View state (no undo history):
	//   light_view      - the *shape* of each light: a point light's range as three
	//                     great circles, a spotlight's cone (inner and outer angle) and
	//                     its axis. Selection-only by default when enabled, since a
	//                     range sphere per light of a lit level is a lot of lines.
	//   light_positions - a marker at every light's position, in the light's own
	//                     colour, whether or not it is selected. Lights carry no mesh
	//                     and no Bounds, so without this an unselected light is
	//                     invisible in the viewport and impossible to find.
	//
	// The shapes are drawn from the same numbers the shaders receive
	// (PointLight::Data: world position, range, direction, cone cosines), so a cone
	// that does not match the lit patch on the floor is a bug in one of the two.
	namespace LightGizmos {

		void Draw(EditorState& state);

		// What the last Draw put on screen. There is no other way to see an overlay
		// from a test besides squinting at a screenshot, so the automation channel's
		// `light_gizmo_info` reports this.
		struct Marker {
			std::string name;
			bool spot = false;
			// Normalized 0..1 across the display, so a test can scale it to whatever
			// size the screenshot is (the backbuffer is not always the client size).
			float x = 0.0f;
			float y = 0.0f;
		};
		struct FrameInfo {
			int lights_drawn = 0;   // lights that had a shape drawn
			int spots_drawn = 0;    // of those, how many were cones
			int segments = 0;       // line segments in the shapes
			std::vector<Marker> markers;
		};
		const FrameInfo& LastFrame();
	}
}
