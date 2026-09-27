#include "EditorHistory.h"

#include <deque>
#include <memory>
#include <vector>

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

		//The open undo group (see BeginGroup), collecting its steps in apply order.
		static bool group_open = false;
		static std::string group_description;
		static std::vector<Action> group_actions;

		static void PushToStack(Action&& action)
		{
			undo_stack.push_back(std::move(action));
			if (undo_stack.size() > MAX_ACTIONS) {
				undo_stack.pop_front();
			}
		}

		//Turns whatever the open group holds into one step on the stack. The children
		//are kept by value inside the closures; shared so the Action stays copyable.
		static bool SealGroup()
		{
			if (group_actions.empty()) {
				return false;
			}
			auto children = std::make_shared<std::vector<Action>>(std::move(group_actions));
			group_actions.clear();
			Action group;
			group.description = group_description + (children->size() > 1
				? " (" + std::to_string(children->size()) + " edits)" : ": " + children->front().description);
			group.undo = [children](EditorState& state) {
				for (auto it = children->rbegin(); it != children->rend(); ++it) {
					it->undo(state);
				}
			};
			group.redo = [children](EditorState& state) {
				for (Action& a : *children) {
					a.redo(state);
				}
			};
			PushToStack(std::move(group));
			return true;
		}

		void BeginGroup(const std::string& description)
		{
			if (group_open) {
				return;
			}
			group_open = true;
			group_description = description;
			group_actions.clear();
		}

		bool EndGroup()
		{
			if (!group_open) {
				return false;
			}
			bool pushed = SealGroup();
			group_open = false;
			group_description.clear();
			return pushed;
		}

		bool GroupOpen()
		{
			return group_open;
		}

		std::string TopDescription()
		{
			return undo_stack.empty() ? std::string() : undo_stack.back().description;
		}

		void Push(Action&& action)
		{
			if (applying) {
				return;
			}
			//A fresh edit invalidates the redone-future, grouped or not.
			redo_stack.clear();
			++revision;
			if (group_open) {
				group_actions.push_back(std::move(action));
				return;
			}
			PushToStack(std::move(action));
		}

		bool CanUndo()
		{
			return !undo_stack.empty() || !group_actions.empty();
		}

		bool CanRedo()
		{
			return !redo_stack.empty();
		}

		bool Undo(EditorState& state, std::string& error)
		{
			//What an open group holds is already applied, so it has to be on the stack
			//before anything below it can be undone (the group stays open).
			SealGroup();
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
			SealGroup();
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
			//A level load inside an agent turn: what the group held belonged to the
			//level that is gone. The group itself stays open for the rest of the turn.
			group_actions.clear();
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
