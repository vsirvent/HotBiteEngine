#include "Outliner.h"
#include "EditorHistory.h"
#include "Inspector.h"
#include "EditorLayout.h"
#include "EntityOps.h"
#include "Selection.h"

#include "imgui.h"
#include <Components/Base.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Outliner {

		//Drag-and-drop payload: the entity's name (NUL-terminated), dropped onto a
		//group header to assign it or onto the panel's empty space to ungroup it.
		static constexpr const char* ENTITY_PAYLOAD = "HB_ENTITY_NAME";

		bool FocusSelected(EditorState& state, EditorCamera& camera, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID) {
				error = "no entity selected";
				return false;
			}
			if (!c->ContainsComponent<Transform>(state.selected_entity)) {
				error = "selected entity has no Transform component";
				return false;
			}
			float3 center = c->GetComponent<Transform>(state.selected_entity).position;
			float radius = 1.5f;
			if (c->ContainsComponent<Bounds>(state.selected_entity)) {
				//The world-space AABB frames the whole object, pivot offset included.
				const box& b = c->GetComponent<Bounds>(state.selected_entity).final_box;
				center = { b.Center.x, b.Center.y, b.Center.z };
				float r = std::sqrtf(b.Extents.x * b.Extents.x +
					b.Extents.y * b.Extents.y + b.Extents.z * b.Extents.z);
				if (r > 0.01f) {
					radius = r;
				}
			}
			if (!camera.Focus(center, radius)) {
				error = "no camera (load a level first)";
				return false;
			}
			return true;
		}

		bool CreateGroup(EditorState& state, const std::string& name, std::string& error)
		{
			if (name.empty()) {
				error = "group name is empty";
				return false;
			}
			if (!state.entity_groups.insert(name).second) {
				error = "group already exists: " + name;
				return false;
			}
			//Undo can simply erase: history is LIFO, so by the time this action is
			//undone every later membership change has been undone and the group is
			//empty again.
			EditorHistory::Push({
				"create group " + name,
				[name](EditorState& s) {
					s.entity_groups.erase(name);
				},
				[name](EditorState& s) {
					s.entity_groups.insert(name);
				} });
			return true;
		}

		bool SetEntityGroup(EditorState& state, const std::string& entity_name,
			const std::string& group, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || c->GetEntityByName(entity_name) == INVALID_ENTITY_ID) {
				error = "entity not found: " + entity_name;
				return false;
			}
			auto prev_it = state.entity_group_of.find(entity_name);
			std::string prev_group = (prev_it != state.entity_group_of.end()) ? prev_it->second : "";
			if (prev_group == group) {
				return true; //no-op, nothing to record
			}
			bool group_created = !group.empty() && state.entity_groups.count(group) == 0;
			if (group.empty()) {
				state.entity_group_of.erase(entity_name);
			}
			else {
				state.entity_groups.insert(group);
				state.entity_group_of[entity_name] = group;
			}
			EditorHistory::Push({
				"group " + entity_name + " -> " + (group.empty() ? "(none)" : group),
				[entity_name, prev_group, group, group_created](EditorState& s) {
					if (group_created) {
						s.entity_groups.erase(group);
					}
					if (prev_group.empty()) {
						s.entity_group_of.erase(entity_name);
					}
					else {
						s.entity_group_of[entity_name] = prev_group;
					}
				},
				[entity_name, group](EditorState& s) {
					if (group.empty()) {
						s.entity_group_of.erase(entity_name);
					}
					else {
						s.entity_groups.insert(group);
						s.entity_group_of[entity_name] = group;
					}
				} });
			return true;
		}

		bool SetEntitiesGroup(EditorState& state, const std::vector<std::string>& entity_names,
			const std::string& group, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				error = "no scene loaded";
				return false;
			}
			//Capture each entity's previous group before touching anything, so one
			//undo closure can put every one of them back where it came from.
			struct Move {
				std::string entity_name;
				std::string prev_group;
			};
			std::vector<Move> moves;
			for (const auto& name : entity_names) {
				if (c->GetEntityByName(name) == INVALID_ENTITY_ID) {
					continue;
				}
				auto it = state.entity_group_of.find(name);
				std::string prev = (it != state.entity_group_of.end()) ? it->second : "";
				if (prev != group) {
					moves.push_back({ name, prev });
				}
			}
			if (moves.empty()) {
				error = "no entities to move";
				return false;
			}
			//A group named for the first time here is created by the move, so undo
			//has to remove it again (matching SetEntityGroup's single-entity rule).
			bool group_created = !group.empty() && state.entity_groups.count(group) == 0;
			auto apply = [moves, group](EditorState& s) {
				if (!group.empty()) {
					s.entity_groups.insert(group);
				}
				for (const auto& m : moves) {
					if (group.empty()) {
						s.entity_group_of.erase(m.entity_name);
					}
					else {
						s.entity_group_of[m.entity_name] = group;
					}
				}
			};
			apply(state);
			EditorHistory::Push({
				"group " + std::to_string(moves.size()) + " entities -> " +
					(group.empty() ? "(none)" : group),
				[moves, group, group_created](EditorState& s) {
					for (const auto& m : moves) {
						if (m.prev_group.empty()) {
							s.entity_group_of.erase(m.entity_name);
						}
						else {
							s.entity_group_of[m.entity_name] = m.prev_group;
						}
					}
					if (group_created) {
						s.entity_groups.erase(group);
					}
				},
				apply });
			return true;
		}

		//Members of `group`, captured for the undo closures of rename/delete.
		static std::vector<std::string> GroupMembers(const EditorState& state, const std::string& group)
		{
			std::vector<std::string> members;
			for (const auto& [entity_name, g] : state.entity_group_of) {
				if (g == group) {
					members.push_back(entity_name);
				}
			}
			return members;
		}

		bool RenameGroup(EditorState& state, const std::string& from,
			const std::string& to, std::string& error)
		{
			if (to.empty()) {
				error = "group name is empty";
				return false;
			}
			if (state.entity_groups.count(from) == 0) {
				error = "unknown group: " + from;
				return false;
			}
			if (from == to) {
				return true; //no-op, nothing to record
			}
			//Renaming onto an existing group merges into it; the member list captured
			//here is what lets undo pull exactly the moved entities back out.
			bool merged = state.entity_groups.count(to) != 0;
			std::vector<std::string> members = GroupMembers(state, from);
			auto apply = [from, to](EditorState& s) {
				s.entity_groups.erase(from);
				s.entity_groups.insert(to);
				for (auto& [entity_name, g] : s.entity_group_of) {
					if (g == from) {
						g = to;
					}
				}
			};
			apply(state);
			EditorHistory::Push({
				"rename group " + from + " -> " + to,
				[from, to, merged, members](EditorState& s) {
					if (!merged) {
						s.entity_groups.erase(to);
					}
					s.entity_groups.insert(from);
					for (const auto& entity_name : members) {
						s.entity_group_of[entity_name] = from;
					}
				},
				apply });
			return true;
		}

		bool DeleteGroup(EditorState& state, const std::string& name, std::string& error)
		{
			if (state.entity_groups.count(name) == 0) {
				error = "unknown group: " + name;
				return false;
			}
			std::vector<std::string> members = GroupMembers(state, name);
			auto apply = [name](EditorState& s) {
				s.entity_groups.erase(name);
				//Members fall back to ungrouped.
				for (auto it = s.entity_group_of.begin(); it != s.entity_group_of.end();) {
					it = (it->second == name) ? s.entity_group_of.erase(it) : std::next(it);
				}
			};
			apply(state);
			EditorHistory::Push({
				"delete group " + name,
				[name, members](EditorState& s) {
					s.entity_groups.insert(name);
					for (const auto& entity_name : members) {
						s.entity_group_of[entity_name] = name;
					}
				},
				apply });
			return true;
		}

		//Makes the last-submitted item accept an entity drop, assigning the entity to
		//`group` (empty = ungroup).
		static void AcceptEntityDrop(EditorState& state, const std::string& group)
		{
			if (!ImGui::BeginDragDropTarget()) {
				return;
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD)) {
				std::string entity_name((const char*)payload->Data);
				std::string error;
				//Dragging a row that belongs to a multi-entity selection drags the
				//whole selection (one undo step); dragging an unselected row moves
				//only that row, leaving the selection alone.
				Coordinator* c = state.world->GetCoordinator();
				Entity dragged = (c != nullptr) ? c->GetEntityByName(entity_name) : INVALID_ENTITY_ID;
				if (dragged != INVALID_ENTITY_ID && Selection::Contains(state, dragged) &&
					Selection::Count(state) > 1) {
					SetEntitiesGroup(state, Selection::Names(state), group, error);
				}
				else {
					SetEntityGroup(state, entity_name, group, error);
				}
			}
			ImGui::EndDragDropTarget();
		}

		//An entity-rename request raised from a row's context menu and consumed at
		//window scope (the rename popup must be opened there, not inside the popup
		//that spawned it), mirroring the group-rename deferral below.
		static std::string entity_rename_from;
		static char entity_rename_to[128] = "";
		static bool open_entity_rename_popup = false;

		//Rows in display order: `visible_rows` is filled as this frame's rows are
		//submitted, `previous_rows` is the last completed frame. Shift-click ranges
		//resolve against the *previous* frame because a click is handled while the
		//list is still being built - the rows below the clicked one do not exist yet.
		//The panel's layout is stable between frames, so the two agree.
		static std::vector<Entity> visible_rows;
		static std::vector<Entity> previous_rows;

		//Selects everything between the primary selection and `entity` in display
		//order, keeping what was already selected (shift-click semantics).
		static void SelectRangeTo(EditorState& state, Entity entity)
		{
			auto to = std::find(previous_rows.begin(), previous_rows.end(), entity);
			auto from = std::find(previous_rows.begin(), previous_rows.end(), state.selected_entity);
			if (to == previous_rows.end() || from == previous_rows.end()) {
				//No usable anchor (nothing selected, or it is not on screen): plain click.
				Selection::Set(state, entity);
				return;
			}
			if (from > to) {
				std::swap(from, to);
			}
			for (auto it = from; it <= to; ++it) {
				Selection::Add(state, *it);
			}
			//Add() made the end of the range primary; the clicked row should be.
			Selection::Add(state, entity);
		}

		static void DrawEntityRow(EditorState& state, EditorCamera& camera,
			const std::string& name, Entity entity)
		{
			visible_rows.push_back(entity);
			bool is_selected = Selection::Contains(state, entity);
			std::string label = name + "##" + std::to_string(entity);
			if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				ImGuiIO& io = ImGui::GetIO();
				if (io.KeyCtrl) {
					Selection::Toggle(state, entity);
				}
				else if (io.KeyShift) {
					SelectRangeTo(state, entity);
				}
				else {
					Selection::Set(state, entity);
				}
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					std::string error;
					if (!FocusSelected(state, camera, error)) {
						state.status_message = "Focus failed: " + error;
					}
				}
			}
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload(ENTITY_PAYLOAD, name.c_str(), name.size() + 1);
				ImGui::TextUnformatted(name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginPopupContextItem()) {
				//The row's operations act on this entity, so make it the selection
				//first (matches right-click-then-act behaviour elsewhere) - unless it
				//is already part of a multi-entity selection, which the menu's
				//group/delete entries then act on as a whole.
				if (!Selection::Contains(state, entity)) {
					Selection::Set(state, entity);
				}
				std::string error;
				if (ImGui::MenuItem("Rename...")) {
					entity_rename_from = name;
					strncpy_s(entity_rename_to, name.c_str(), sizeof(entity_rename_to) - 1);
					open_entity_rename_popup = true;
				}
				bool can_copy = EntityOps::CanCopySelected(state);
				if (ImGui::MenuItem("Copy", "Ctrl+C", false, can_copy)) {
					if (!EntityOps::CopySelected(state, error)) {
						state.status_message = "Copy failed: " + error;
					}
				}
				if (ImGui::MenuItem("Cut", "Ctrl+X", false, can_copy)) {
					if (!EntityOps::CutSelected(state, error)) {
						state.status_message = "Cut failed: " + error;
					}
				}
				if (ImGui::MenuItem("Paste", "Ctrl+V", false,
					state.clipboard.kind != EntityClipboard::Kind::None)) {
					if (!EntityOps::Paste(state, error)) {
						state.status_message = "Paste failed: " + error;
					}
				}
				ImGui::Separator();
				//Group moves and delete act on the whole selection, which is why the
				//labels count it: right-clicking one row of a multi-entity selection
				//and picking a group moves all of them, in one undo step.
				size_t selected_count = Selection::Count(state);
				std::string move_label = (selected_count > 1)
					? "Move " + std::to_string(selected_count) + " entities to group"
					: std::string("Move to group");
				if (ImGui::BeginMenu(move_label.c_str())) {
					//Copy the current group: the move mutates entity_group_of, which
					//would invalidate an iterator held across the MenuItems. With a
					//multi-entity selection there is no single "current" group, so the
					//checkmark is only meaningful for a lone entity.
					auto it = state.entity_group_of.find(name);
					std::string current_group = (selected_count > 1 || it == state.entity_group_of.end())
						? "" : it->second;
					bool mark_current = (selected_count == 1);
					std::vector<std::string> targets(state.entity_groups.begin(), state.entity_groups.end());
					targets.insert(targets.begin(), std::string()); //"(none)" = ungroup
					std::vector<std::string> names = Selection::Names(state);
					for (const auto& g : targets) {
						const char* label = g.empty() ? "(none)" : g.c_str();
						bool checked = mark_current && g == current_group;
						if (ImGui::MenuItem(label, nullptr, checked)) {
							std::string err;
							if (!SetEntitiesGroup(state, names, g, err)) { //records undo history
								state.status_message = "Move to group failed: " + err;
							}
						}
					}
					ImGui::EndMenu();
				}
				ImGui::Separator();
				std::string delete_label = (selected_count > 1)
					? "Delete " + std::to_string(selected_count) + " entities"
					: std::string("Delete");
				if (ImGui::MenuItem(delete_label.c_str(), "Del", false,
					EntityOps::CanDeleteSelected(state))) {
					//Confirmation for a multi-entity delete is the shared modal driven
					//from the main loop, not this transient popup.
					state.delete_requested = true;
				}
				ImGui::EndPopup();
			}
		}

		void Draw(EditorState& state, EditorCamera& camera)
		{
			ImGui::Begin(EditorLayout::OUTLINER_WINDOW);

			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				ImGui::TextUnformatted("No scene loaded.");
				ImGui::End();
				return;
			}

			//Group creation. The popup buffer survives across frames while it is open.
			static char new_group_name[128] = "";
			if (ImGui::SmallButton("+ Group")) {
				new_group_name[0] = '\0';
				ImGui::OpenPopup("##new_group");
			}
			ImGui::SetItemTooltip("Create a new entity group.\nDrag entities onto a group to move them into it.");
			if (ImGui::BeginPopup("##new_group")) {
				if (ImGui::IsWindowAppearing()) {
					ImGui::SetKeyboardFocusHere();
				}
				bool commit = ImGui::InputText("##name", new_group_name, sizeof(new_group_name),
					ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				commit |= ImGui::Button("Create");
				if (commit && new_group_name[0] != '\0') {
					std::string error;
					CreateGroup(state, new_group_name, error); //records undo history
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::Separator();

			//GetEntites() (name preserved as spelled in the engine) returns the full
			//name -> Entity map; order it alphabetically for display and partition it
			//into the assigned groups.
			std::vector<std::pair<std::string, Entity>> sorted;
			sorted.reserve(c->GetEntites().size());
			for (const auto& [name, entity] : c->GetEntites()) {
				//Parked (cut) entities still live in the world so a paste can clone
				//them and an undo can revive them; they are hidden from the panel.
				if (c->ContainsComponent<Base>(entity) && !EntityOps::IsParkedName(name)) {
					sorted.emplace_back(name, entity);
				}
			}
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
				int cmp = _stricmp(a.first.c_str(), b.first.c_str());
				return (cmp != 0) ? cmp < 0 : a.first < b.first;
				});

			std::map<std::string, std::vector<const std::pair<std::string, Entity>*>> per_group;
			for (const auto& g : state.entity_groups) {
				per_group[g]; //empty groups still show as tree nodes
			}
			std::vector<const std::pair<std::string, Entity>*> ungrouped;
			for (const auto& item : sorted) {
				auto it = state.entity_group_of.find(item.first);
				if (it != state.entity_group_of.end() && per_group.count(it->second) != 0) {
					per_group[it->second].push_back(&item);
				}
				else {
					ungrouped.push_back(&item);
				}
			}

			//Row order is rebuilt from scratch each frame; the frame just finished
			//becomes the reference a shift-click range resolves against.
			previous_rows = visible_rows;
			visible_rows.clear();

			//Structural edits (rename/delete) are deferred to after the iteration so
			//the loop never mutates the containers it is walking.
			static std::string rename_from;
			static char rename_to[128] = "";
			bool open_rename_popup = false;
			std::string delete_group;

			for (const auto& [group, members] : per_group) {
				//"###" keeps the tree node's ID independent of the member count shown
				//in the label.
				std::string label = group + " (" + std::to_string(members.size()) + ")###grp_" + group;
				//A group is highlighted when every one of its (non-empty) members is
				//selected: selecting a group *is* selecting the entities inside it.
				bool group_selected = !members.empty();
				for (const auto* item : members) {
					if (!Selection::Contains(state, item->second)) {
						group_selected = false;
						break;
					}
				}
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
					ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (group_selected) {
					flags |= ImGuiTreeNodeFlags_Selected;
				}
				bool open = ImGui::TreeNodeEx(label.c_str(), flags);
				//OpenOnArrow means a click on the label itself is a selection, not a
				//fold; Ctrl adds the group's entities to the current selection.
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
					Selection::SelectGroup(state, group, ImGui::GetIO().KeyCtrl);
				}
				AcceptEntityDrop(state, group);
				if (ImGui::BeginPopupContextItem()) {
					if (ImGui::MenuItem("Select entities")) {
						Selection::SelectGroup(state, group, false);
					}
					if (ImGui::MenuItem("Rename...")) {
						rename_from = group;
						strncpy_s(rename_to, group.c_str(), sizeof(rename_to) - 1);
						open_rename_popup = true;
					}
					if (ImGui::MenuItem("Delete group")) {
						delete_group = group;
					}
					ImGui::EndPopup();
				}
				if (open) {
					for (const auto* item : members) {
						DrawEntityRow(state, camera, item->first, item->second);
					}
					ImGui::TreePop();
				}
			}

			//Ungrouped entities at the root, below the groups.
			for (const auto* item : ungrouped) {
				DrawEntityRow(state, camera, item->first, item->second);
			}

			//The leftover empty space doubles as the "no group" drop target, so
			//dragging an entity out of a group and onto the panel background works.
			ImVec2 avail = ImGui::GetContentRegionAvail();
			avail.x = (std::max)(avail.x, 1.0f);
			avail.y = (std::max)(avail.y, ImGui::GetTextLineHeight());
			ImGui::Dummy(avail);
			AcceptEntityDrop(state, "");

			//The rename request comes from inside the group's context popup; the
			//rename popup itself must be opened from window scope.
			if (open_rename_popup) {
				ImGui::OpenPopup("##rename_group");
			}
			if (ImGui::BeginPopup("##rename_group")) {
				if (ImGui::IsWindowAppearing()) {
					ImGui::SetKeyboardFocusHere();
				}
				bool commit = ImGui::InputText("##name", rename_to, sizeof(rename_to),
					ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				commit |= ImGui::Button("Rename");
				if (commit && rename_to[0] != '\0' && rename_from != rename_to) {
					std::string error;
					RenameGroup(state, rename_from, rename_to, error); //records undo history
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (!delete_group.empty()) {
				std::string error;
				DeleteGroup(state, delete_group, error); //records undo history
			}

			//Entity rename popup, opened from a row's context menu (same window-scope
			//deferral as the group rename above).
			if (open_entity_rename_popup) {
				ImGui::OpenPopup("##rename_entity");
				open_entity_rename_popup = false;
			}
			if (ImGui::BeginPopup("##rename_entity")) {
				if (ImGui::IsWindowAppearing()) {
					ImGui::SetKeyboardFocusHere();
				}
				bool commit = ImGui::InputText("##ename", entity_rename_to, sizeof(entity_rename_to),
					ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				commit |= ImGui::Button("Rename");
				if (commit && entity_rename_to[0] != '\0') {
					std::string error;
					if (!EntityOps::RenameEntity(state, entity_rename_from, entity_rename_to, error)) {
						state.status_message = "Rename failed: " + error;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::End();
		}

	}
}
