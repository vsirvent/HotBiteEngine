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

		// Whether the live scene differs from what was last written to disk: a Push,
		// Undo or Redo since the last MarkSaved()/Clear(). An Undo counts too - it
		// makes the live scene diverge from disk exactly as a fresh edit would.
		bool HasUnsavedChanges();
		// Called after a successful save: everything accumulated since is now on disk.
		void MarkSaved();

		// Undo groups: every Action pushed between BeginGroup and EndGroup becomes ONE
		// step on the stack, undone in reverse order and redone in order. This is how
		// a Claude panel turn - any number of edits made through the automation
		// channel while the agent answers one message - is undone with one Ctrl+Z.
		//
		// A group holds only what was pushed while it was open, so a user edit made in
		// the middle of an agent turn joins that turn. An Undo/Redo while a group is
		// open first seals what the group holds so far as its own step (the LIFO
		// guarantee needs the stack to hold everything that was applied) and keeps the
		// group open for whatever follows. An empty group pushes nothing. Groups do not
		// nest: BeginGroup while one is open just keeps the open one.
		void BeginGroup(const std::string& description);
		// Seals the group. True when it pushed a step (the group was not empty).
		bool EndGroup();
		bool GroupOpen();
		// The description of the step Undo would apply next ("" when none).
		std::string TopDescription();
	}
}
