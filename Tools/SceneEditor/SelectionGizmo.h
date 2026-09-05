#pragma once

#include "SceneEditor.h"
#include "imgui.h"

namespace HotBiteEditor {
	namespace SelectionGizmo {
		// Overlays the selected entity with a transform gizmo at the rendered world
		// pivot (parent transform included) and, when it has a Bounds component,
		// its world-space AABB. Drawn into ImGui's background draw list (over the
		// 3D scene, under the editor panels) by projecting world points through
		// the active camera, so it needs no engine changes.
		// The tool follows EditorState::gizmo_mode (Edit menu / 1-2-3 keys):
		//  - Translate: XYZ axis arrows; dragging one moves the entity along it.
		//  - Rotate: three axis circles; dragging one spins the entity around that
		//    local axis by the angle the cursor sweeps in the circle's plane.
		//  - Scale: axis lines with square caps scale per-axis by the ratio of the
		//    cursor's distance along the axis; the center square scales uniformly
		//    by the cursor's screen distance from the pivot. Clamped positive, so
		//    dragging through the pivot can't mirror the mesh.
		// Every edit goes through Inspector::ApplyTransform so save bookkeeping
		// (instance sync / FBX overrides) matches a manual Inspector edit.
		// Left-clicking the viewport (not on an axis, not over a panel) selects
		// the entity under the cursor: nearest hit against physics colliders and
		// world AABBs (camera-enclosing boxes skipped so terrain-sized volumes
		// don't shadow props), or clears the selection on empty space/sky.
		void Draw(EditorState& state);

		// Nearest entity along a world-space ray - the same test a viewport click
		// runs (physics colliders + world AABBs, camera-enclosing AABB hits skipped).
		// `ray_dir` need not be normalized. On a hit, `out_distance` (when non-null)
		// receives the distance from `ray_origin` in world units. Used by the DOF
		// autofocus to measure the depth at the center of the view.
		HotBite::Engine::ECS::Entity RaycastScene(HotBite::Engine::ECS::Coordinator* c,
			const HotBite::Engine::float3& ray_origin,
			const HotBite::Engine::float3& ray_dir,
			float* out_distance = nullptr);

		// Applies one gizmo interaction to the current selection without a live
		// mouse drag - the same math and undo/history bookkeeping a real drag
		// produces (including grid snapping), for automation and tests that need
		// to exercise translate/rotate/scale without a literal mouse-driven
		// session (see EditorAutomation.cpp's simulate_gizmo_drag). `axis` is
		// 0/1/2 for X/Y/Z, or -1 for the scale tool's uniform handle (ignored in
		// Translate/Rotate). `amount` is exactly what a real drag would have
		// measured: a world-space distance along the axis (Translate), radians
		// swept since mouse-down (Rotate), or a scale factor since mouse-down
		// (Scale). No-op when nothing in the selection is editable.
		void SimulateDrag(EditorState& state, GizmoMode mode, int axis, float amount);

		// World-space picking ray from the camera through a screen pixel (`mouse`,
		// in the same pixel space as `display`/ImGui's io.DisplaySize). False when
		// there is no camera to cast from. Shared with MaskPaint's viewport brush,
		// which needs the identical unprojection the gizmo/click-picking already use.
		bool ComputeMouseRay(HotBite::Engine::ECS::Coordinator* c, const ImVec2& display,
			const ImVec2& mouse, HotBite::Engine::vector3d& origin, HotBite::Engine::vector3d& dir);
	}
}
