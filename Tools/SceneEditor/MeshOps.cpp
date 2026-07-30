#include "MeshOps.h"
#include "ComponentOps.h"

#include <World.h>
#include <Components/Base.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace MeshOps {

		float SuggestedRatio(const Core::MeshData* data) {
			float coarsest = 1.0f;
			if (data != nullptr) {
				for (const Core::MeshData::MeshLod& lod : data->lods) {
					if (lod.ratio < coarsest) {
						coarsest = lod.ratio;
					}
				}
			}
			float ratio = coarsest * 0.5f;
			//Below a twentieth of the model there is not enough left for the reduction
			//to be about the shape any more, and the chain has as many levels as it is
			//ever going to use.
			if (ratio < 0.05f) {
				ratio = 0.05f;
			}
			return ratio;
		}

		bool GenerateLod(EditorState& state, const std::string& entity_name, float ratio,
			std::string& generated_name, std::string& error) {
			generated_name.clear();
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			if (c == nullptr) {
				error = "no level loaded";
				return false;
			}
			const Entity e = c->GetEntityByName(entity_name);
			if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Mesh>(e)) {
				error = "entity '" + entity_name + "' has no Mesh";
				return false;
			}
			Core::MeshData* data = c->GetComponent<Mesh>(e).GetData();
			if (data == nullptr) {
				error = "entity '" + entity_name + "' has no mesh data";
				return false;
			}
			//Always simplified from level 0, never from the level above it. Chaining
			//reductions compounds the error - each one approximates an approximation -
			//and the ratios stop meaning what they say, since a level's ratio is its
			//share of the *full* mesh and is what the renderer selects on.
			const std::string source = data->name;
			if (!state.world->GenerateMeshLod(source, ratio, generated_name, error)) {
				return false;
			}
			//Onto the chain through the component, exactly as the mesh picker's own
			//"Add level" does: it is the one path that records the edit for save and
			//pushes the history action.
			nlohmann::json block = ComponentOps::GetValue(state, entity_name, Mesh::NAME);
			if (!block.contains("lods") || !block["lods"].is_array()) {
				block["lods"] = nlohmann::json::array();
			}
			block["lods"].push_back(nlohmann::json{ {"name", generated_name},
													{"distance", 0.0f} });
			if (!ComponentOps::SetValue(state, entity_name, Mesh::NAME, block, error)) {
				//The mesh asset stays. It is a registered asset now, usable by anything
				//that draws a mesh, and destroying it here would dangle every pointer
				//into the collection it lives in (see World::RemoveMaterial for the
				//same reason applied to materials).
				return false;
			}
			return true;
		}
	}
}
