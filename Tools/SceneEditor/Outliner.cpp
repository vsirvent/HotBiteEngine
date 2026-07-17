#include "Outliner.h"
#include "Inspector.h"
#include "EditorLayout.h"

#include "imgui.h"
#include <Components/Base.h>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Outliner {

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

		void Draw(EditorState& state, EditorCamera& camera)
		{
			EditorLayout::PlaceOutliner(state);
			ImGui::Begin("Outliner");

			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				ImGui::TextUnformatted("No scene loaded.");
				ImGui::End();
				return;
			}

			//GetEntites() (name preserved as spelled in the engine) returns the full
			//name -> Entity map directly, avoiding a per-frame list rebuild.
			for (const auto& [name, entity] : c->GetEntites()) {
				if (!c->ContainsComponent<Base>(entity)) {
					continue;
				}
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
			}

			ImGui::End();
		}

	}
}
