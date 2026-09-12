#include "SplatOps.h"

#include <World.h>
#include <Components/Base.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace SplatOps {

		bool GenerateProxy(EditorState& state, const std::string& entity_name,
			int resolution, float ratio, std::string& generated_name, std::string& error) {
			generated_name.clear();
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			if (c == nullptr) {
				error = "no level loaded";
				return false;
			}
			const Entity e = c->GetEntityByName(entity_name);
			if (e == INVALID_ENTITY_ID || !c->ContainsComponent<SplatCloud>(e)) {
				error = "entity '" + entity_name + "' has no SplatCloud";
				return false;
			}
			Core::SplatCloudData* data = c->GetComponent<SplatCloud>(e).data;
			if (data == nullptr) {
				error = "entity '" + entity_name + "' has no splat cloud data";
				return false;
			}
			return state.world->GenerateSplatProxy(data->GetName(), resolution, ratio,
				generated_name, error);
		}

		bool RemoveProxy(EditorState& state, const std::string& entity_name, std::string& error) {
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			if (c == nullptr) {
				error = "no level loaded";
				return false;
			}
			const Entity e = c->GetEntityByName(entity_name);
			if (e == INVALID_ENTITY_ID || !c->ContainsComponent<SplatCloud>(e)) {
				error = "entity '" + entity_name + "' has no SplatCloud";
				return false;
			}
			Core::SplatCloudData* data = c->GetComponent<SplatCloud>(e).data;
			if (data == nullptr) {
				error = "entity '" + entity_name + "' has no splat cloud data";
				return false;
			}
			return state.world->RemoveSplatProxy(data->GetName(), error);
		}
	}
}
