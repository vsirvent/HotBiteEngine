#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The editor-wide undo/redo stack (Edit/Undo, Edit/Redo, Ctrl+Z/Ctrl+Y, and the
	// automation channel's `undo`/`redo`).
	//
	// == Rule for every new editor command ==
	// Any command that mutates the scene or its editor bookkeeping (transforms,
	// spawned/removed instances, groups, and whatever gets added next) MUST push an
	// Action right after its mutation succeeds, no matter which surface triggered it
	// (panel widget, menu item, or automation command). The cleanest shape is the one
	// the existing commands use: one shared helper performs the mutation and pushes
	// the Action, and every surface calls that helper. Pushing is safe to do
	// unconditionally from such helpers: while an undo/redo is being applied, Push()
	// is a no-op, so an Action's closures may re-enter the same helpers without
	// recording new history.
	//
	// Closures must not capture ECS::Entity ids for anything an undo might destroy
	// and a redo re-create (ids are recycled): capture entity *names* and resolve
	// them at apply time. Capturing by value the before/after state you need is
	// correct because undo/redo is strictly LIFO - when an Action's undo() runs, the
	// world is guaranteed to be in the exact state it was right after that Action was
	// first applied (provided everything mutating went through this history).
	//
	// Out of scope by design: selection, camera, gizmo mode, panel visibility and
	// render settings (view state, not scene state), Simulate Physics (its settling
	// writes transforms outside any command), and File/Import Object (copies a file
	// into the project; deleting user files on undo is worse than not undoing).
	namespace EditorHistory {

		// A single undoable step, pushed *after* the edit has already been applied.
		// undo() must restore the pre-edit state, redo() must re-apply the edit;
		// both run on the main thread and must succeed given the LIFO guarantee.
		struct Action {
			std::string description;             // human-readable, e.g. "move box1"
			std::function<void(EditorState&)> undo;
			std::function<void(EditorState&)> redo;
		};

		void Push(Action&& action);

		bool CanUndo();
		bool CanRedo();

		// Applies the top of the respective stack and reports what it did through
		// state.status_message. False with `error` set when the stack is empty.
		bool Undo(EditorState& state, std::string& error);
		bool Redo(EditorState& state, std::string& error);

		// Drops both stacks (level load).
		void Clear();
	}
}
