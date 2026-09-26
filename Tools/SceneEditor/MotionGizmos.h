#pragma once

#include "SceneEditor.h"
#include <string>
#include <vector>

namespace HotBiteEditor {
	// Viewport gizmos for the things that move and the things that push: the Platform
	// and LinearPlatform components (Engine/Components/Platform.h) and the Force
	// component (Engine/Components/Force.h).
	//
	// Drawn into ImGui's background draw list by projecting world points through the
	// active camera, exactly like the collider overlay and the light gizmos, so none of
	// this involves a render-pipeline change.
	//
	// These three components are the engine's only authoring surface that is *entirely*
	// invisible. A light at least lights something and a collider at least stops
	// something; a force field is geometry that exists nowhere in the frame, and a
	// platform at rest - which is what the editor always shows, since the Scene Editor
	// keeps physics paused - is indistinguishable from a static prop. Every number in
	// them had to be verified by starting the game and watching. So:
	//
	//   Platform       - the oscillation drawn as its actual travel, a segment
	//                    `amplitude` either side of the authored centre, plus a circle
	//                    around the spin axis when the platform turns.
	//   LinearPlatform - the path, from the authored position to it plus `travel`.
	//   Force          - PROJECTION as the cylinder it really is (`radius` wide,
	//                    reaching `range`), with chevrons along the axis whose size
	//                    tracks the force at that distance, so the origin_force ramp is
	//                    something you can see rather than something you infer. TOUCH
	//                    has no volume, so it is an arrow at the entity: the direction
	//                    is the only thing there is to show.
	//
	// The shapes come from the same numbers the systems use - ForceSystem::WorldDirection
	// resolves Force::local_dir for both - so a gizmo that disagrees with what a body
	// does in the simulation is a bug in one of the two rather than two drifting copies
	// of one rule.
	//
	// Two independent switches, both View state (no undo history), each Off/Selection/
	// All like the light and collider overlays.
	namespace MotionGizmos {

		void Draw(EditorState& state);

		// What the last Draw put on screen. An overlay is otherwise only visible by
		// squinting at a screenshot, so the automation channel's `motion_gizmo_info`
		// reports this.
		struct Marker {
			std::string name;
			// "platform", "linear" or "force"
			std::string kind;
			// For a force, the class it was drawn as ("TOUCH"/"PROJECTION"); for a
			// platform, which motions it has ("linear", "spin", "linear+spin").
			std::string detail;
			// Normalized 0..1 across the display, so a test can scale it to whatever size
			// the screenshot is (the backbuffer is not always the client size).
			float x = 0.0f;
			float y = 0.0f;
		};
		struct FrameInfo {
			int platforms_drawn = 0;  // Platform components that had a shape drawn
			int linears_drawn = 0;    // LinearPlatform components
			int forces_drawn = 0;     // Force components with a shape (not NONE)
			int segments = 0;         // line segments across all of them
			std::vector<Marker> markers;
		};
		const FrameInfo& LastFrame();
	}
}
