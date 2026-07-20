#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {

	// Edit/Simulate Physics.
	//
	// The editor authors a scene, it doesn't play it, so the simulation is paused by
	// default (see World::SetPhysicsPause in SceneEditor.cpp). Switching it on is a
	// *preview*: the user watches bodies fall and settle to check colliders, masses
	// and placement. Everything the simulation does to the scene is therefore
	// throwaway - switching the preview back off rewinds every simulated entity to
	// the pose it had when the preview started, so a preview leaves the level exactly
	// as it found it and cannot be saved by accident.
	//
	// The one thing that survives a rewind is authoring done *while* previewing:
	// Inspector's CommitTransformEdit overwrites the entity's baseline entry on every
	// transform edit, so a gizmo drag mid-simulation rewinds to where the user
	// dragged it. In other words the user edits the authored transform and the
	// simulation runs on top of it, which is the point of the mode - drop a ball,
	// see it land badly, move it, drop it again.
	//
	// Only entities with a non-static Physics body are captured, because
	// PhysicsSystem::Update writes back to nothing else.
	namespace PhysicsPreview {

		// Switches the preview on (capture baseline, unpause) or off (rewind, pause).
		// Idempotent: setting it to the state it is already in does nothing, so a
		// rewind can never be triggered twice and lose an interim edit.
		void SetEnabled(EditorState& state, bool enabled);

		bool IsEnabled(const EditorState& state);

		// Drops the baseline without rewinding. For level teardown only, where the
		// entities are about to go away.
		void Reset(EditorState& state);
	}
}
