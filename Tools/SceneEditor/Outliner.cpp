#include "Outliner.h"
#include "EditorHistory.h"
#include "Inspector.h"
#include "EditorLayout.h"

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
				SetEntityGroup(state, entity_name, group, error); //records undo history
			}
			ImGui::EndDragDropTarget();
		}

		static void DrawEntityRow(EditorState& state, EditorCamera& camera,
			const std::string& name, Entity entity)
		{
			bool is_selected = (entity == state.selected_entity);
			std::string label = name + "##" + std::to_string(entity);
			if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				state.selected_entity = entity;
				Inspector::RefreshEulerCache(state);
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
				if (ImGui::BeginMenu("Move to group")) {
					//Copy the current group: SetEntityGroup mutates entity_group_of,
					//which would invalidate an iterator held across the MenuItems.
					auto it = state.entity_group_of.find(name);
					std::string current_group = (it != state.entity_group_of.end()) ? it->second : "";
					std::string error;
					if (ImGui::MenuItem("(none)", nullptr, current_group.empty())) {
						SetEntityGroup(state, name, "", error); //records undo history
					}
					//entity_groups is only mutated here through the same no-op-safe
					//helper, and a click closes the popup; iterate over a copy anyway
					//so a recorded merge/create can never invalidate the loop.
					std::vector<std::string> groups(state.entity_groups.begin(), state.entity_groups.end());
					for (const auto& g : groups) {
						if (ImGui::MenuItem(g.c_str(), nullptr, g == current_group)) {
							SetEntityGroup(state, name, g, error); //records undo history
						}
					}
					ImGui::EndMenu();
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
				if (c->ContainsComponent<Base>(entity)) {
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
				bool open = ImGui::TreeNodeEx(label.c_str(),
					ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
					ImGuiTreeNodeFlags_SpanAvailWidth);
				AcceptEntityDrop(state, group);
				if (ImGui::BeginPopupContextItem()) {
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

			ImGui::End();
		}

	}
}
