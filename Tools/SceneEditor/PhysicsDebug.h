#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// Wireframe overlay of the physics colliders, drawn over the 3D viewport in the
	// same background draw list as the selection gizmo.
	//
	// This exists to answer one question directly: does an entity's collider still
	// match its mesh? Collision shapes are built once, from the Transform the entity
	// had when its body was created, so a scale or rotation that fails to reach the
	// collider leaves a body that is the wrong size or in the wrong place - and
	// nothing about the rendered scene shows it until something falls through the
	// floor. The overlay draws the collider exactly as reactphysics3d holds it:
	// every point goes through the collider's local-to-body transform and then the
	// rigid body's own world transform, so a mismatch between the green wireframe
	// and the mesh *is* the bug, not a drawing artifact.
	//
	// Mesh (concave) colliders are drawn from the same ShapeData triangles the
	// shape was built from, scaled by the live shape's scale factor, which is what
	// makes a double-applied or missing scale immediately visible.
	namespace PhysicsDebug {

		// Drawn for the selection only (the default when enabled) or for every
		// entity with a body. "All" is genuinely heavy on a scene full of terrain
		// meshes, hence the budget below.
		void Draw(EditorState& state);

		// Segments drawn per frame before the overlay gives up on the rest. A
		// concave terrain mesh alone can run to tens of thousands of triangles, and
		// ImGui draws these on the CPU; past this the editor stops being
		// interactive, which is worse than an incomplete picture. When the budget
		// truncates the drawing, the overlay says so on screen.
		constexpr int SEGMENT_BUDGET = 60000;
	}
}
