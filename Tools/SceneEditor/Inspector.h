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

		// Quaternion -> Euler degrees (pitch=X, yaw=Y, roll=Z), the inverse of
		// float3_to_quaternion's composition convention. Used by RefreshEulerCache and
		// by the rotate gizmo to feed a quaternion result through ApplyTransform's
		// Euler channel.
		HotBite::Engine::float3 QuaternionToEulerDegrees(const HotBite::Engine::float4& q);

		// Programmatic equivalent of editing the Inspector's DragFloat3 fields on the
		// currently selected entity: applies whichever channels are non-null and runs
		// the same save bookkeeping (placed-instance sync / FBX override tracking).
		// Returns false with `error` set if there is no selected entity with a Transform.
		// Records one EditorHistory action per call; pass record_history=false only
		// when the caller coalesces a continuous edit itself (the gizmo records a
		// single action per drag via GetSnapshot at grab + RecordTransformEdit at
		// release).
		bool ApplyTransform(EditorState& state,
			const HotBite::Engine::float3* position,
			const HotBite::Engine::float3* scale,
			const HotBite::Engine::float3* euler_degrees,
			std::string& error,
			bool record_history = true);

		// A full copy of an entity's Transform channels, the unit the undo history
		// stores (entities are addressed by name because ids get recycled across a
		// place-undo/redo cycle). Declared in SceneEditor.h so EditorState can hold
		// some; this is the name the rest of the editor knows it by.
		using TransformSnapshot = HotBiteEditor::TransformSnapshot;

		// Captures `entity_name`'s current Transform. False when the entity is gone
		// or has no Transform.
		bool GetSnapshot(EditorState& state, const std::string& entity_name, TransformSnapshot& out);

		// Restores a snapshot with the full save bookkeeping of a manual edit (and
		// the Euler cache refresh when the entity is selected). No history is
		// recorded: this is the primitive undo/redo closures are built from.
		bool ApplySnapshot(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& snapshot, std::string& error);

		// ApplySnapshot without any of the save bookkeeping: the Transform and the
		// physics body move, but the entity is not marked as edited (no FBX override,
		// no placed-instance sync) and the preview baseline is left alone. This is the
		// physics preview's rewind (PhysicsPreview.h) - putting the scene back exactly
		// where it was is not an authoring change, and routing it through
		// ApplySnapshot would mark every simulated entity dirty for save.
		bool RestoreSnapshot(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& snapshot, std::string& error);

		// Pushes one undo action for an already-applied transform edit of
		// `entity_name`, from `before` to its current Transform. No-op when nothing
		// actually changed, so callers can invoke it unconditionally at edit end.
		void RecordTransformEdit(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& before);

		// The multi-entity form: one action covering an already-applied edit of
		// several entities, so a gizmo drag on a whole selection undoes in a single
		// step. `befores` is parallel to `entity_names`. Entities whose transform did
		// not actually change are dropped, and nothing is pushed when none did.
		void RecordTransformEdits(EditorState& state,
			const std::vector<std::string>& entity_names,
			const std::vector<TransformSnapshot>& befores);

		// ApplySnapshot for a set of entities (used by the undo/redo closures of a
		// multi-entity transform edit).
		void ApplySnapshots(EditorState& state, const std::vector<std::string>& entity_names,
			const std::vector<TransformSnapshot>& snapshots);

		// Editable widgets for a component this binary has no hand-written editor for,
		// built from the shape of its serialized JSON alone: numbers, flags and names,
		// with nested objects and arrays shown as text rather than guessed at. Returns
		// true when something changed, and sets `finished` when the change is complete
		// (a committed text field or a checkbox click, as opposed to a drag in
		// progress) so callers can coalesce a drag into one undo action.
		//
		// Shared with the Templates panel, which edits component blocks that have no
		// live entity behind them at all - the grid needs nothing but the JSON, which
		// is exactly why it is the fallback in both places.
		bool DrawJsonGrid(nlohmann::json& value, bool* finished = nullptr);
	}
}
