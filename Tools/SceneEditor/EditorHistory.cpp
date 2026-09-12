#include "EditorHistory.h"

#include <deque>

namespace HotBiteEditor {
	namespace EditorHistory {

		//Oldest actions are dropped past this depth; unbounded growth is pointless
		//for an interactive session and every closure pins captured state.
		static constexpr size_t MAX_ACTIONS = 512;

		static std::deque<Action> undo_stack;
		static std::deque<Action> redo_stack;
		//True while an Action's undo()/redo() closure runs, so mutation helpers that
		//push history unconditionally don't record the replay as a fresh edit.
		static bool applying = false;
		//Monotonic "the live scene changed" counter, and its value at the last save.
		//Undo/redo bump it too: both make the live scene differ from what is on disk,
		//exactly as a fresh edit would (see HasUnsavedChanges).
		static uint64_t revision = 0;
		static uint64_t saved_revision = 0;

		void Push(Action&& action)
		{
			if (applying) {
				return;
			}
			undo_stack.push_back(std::move(action));
			if (undo_stack.size() > MAX_ACTIONS) {
				undo_stack.pop_front();
			}
			//A fresh edit invalidates the redone-future.
			redo_stack.clear();
			++revision;
		}

		bool CanUndo()
		{
			return !undo_stack.empty();
		}

		bool CanRedo()
		{
			return !redo_stack.empty();
		}

		bool Undo(EditorState& state, std::string& error)
		{
			if (undo_stack.empty()) {
				error = "nothing to undo";
				return false;
			}
			Action action = std::move(undo_stack.back());
			undo_stack.pop_back();
			applying = true;
			action.undo(state);
			applying = false;
			state.status_message = "Undone: " + action.description;
			redo_stack.push_back(std::move(action));
			++revision;
			return true;
		}

		bool Redo(EditorState& state, std::string& error)
		{
			if (redo_stack.empty()) {
				error = "nothing to redo";
				return false;
			}
			Action action = std::move(redo_stack.back());
			redo_stack.pop_back();
			applying = true;
			action.redo(state);
			applying = false;
			state.status_message = "Redone: " + action.description;
			undo_stack.push_back(std::move(action));
			++revision;
			return true;
		}

		void Clear()
		{
			undo_stack.clear();
			redo_stack.clear();
			revision = 0;
			saved_revision = 0;
		}

		bool HasUnsavedChanges()
		{
			return revision != saved_revision;
		}

		void MarkSaved()
		{
			saved_revision = revision;
		}

	}
}
