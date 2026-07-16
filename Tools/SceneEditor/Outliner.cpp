#include "Outliner.h"
#include "Inspector.h"

#include "imgui.h"
#include <Components/Base.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Outliner {

		void Draw(EditorState& state)
		{
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
				if (ImGui::Selectable(label.c_str(), is_selected)) {
					state.selected_entity = entity;
					Inspector::RefreshEulerCache(state);
				}
			}

			ImGui::End();
		}

	}
}
