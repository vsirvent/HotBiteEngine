#include "PhysicsPreview.h"
#include "Inspector.h"

#include <ECS/Coordinator.h>
#include <Components/Base.h>
#include <Components/Physics.h>
#include <mutex>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace PhysicsPreview {

		//The entities the simulation can move, and therefore the ones that have to be
		//rewound: PhysicsSystem::Update only writes a Transform back for a body that
		//is not STATIC.
		static bool IsSimulated(Coordinator* c, Entity e)
		{
			if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Transform>(e) ||
				!c->ContainsComponent<Physics>(e)) {
				return false;
			}
			const Physics& ph = c->GetComponent<Physics>(e);
			return ph.body != nullptr && ph.type != reactphysics3d::BodyType::STATIC;
		}

		static void Capture(EditorState& state)
		{
			state.physics_preview_baseline.clear();
			Coordinator* c = (state.world != nullptr) ? state.world->GetCoordinator() : nullptr;
			if (c == nullptr) {
				return;
			}
			//Read the bodies' poses under the physics lock, as the background thread
			//may still be mid-tick from a previous preview.
			std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
			for (const auto& [name, entity] : c->GetEntites()) {
				if (!IsSimulated(c, entity)) {
					continue;
				}
				const Transform& t = c->GetComponent<Transform>(entity);
				state.physics_preview_baseline[name] = { t.position, t.rotation, t.scale };
			}
			state.status_message = "Physics preview on: simulating " +
				std::to_string(state.physics_preview_baseline.size()) + " entities";
		}

		static void Rewind(EditorState& state)
		{
			//Inspector::RestoreSnapshot moves the Transform *and* teleports the body
			//back (zeroing its velocities), without marking anything as edited: a
			//preview must not make the level dirty.
			//
			//Hold the physics lock across the *whole* rewind, not just the body
			//teleports RestoreSnapshot locks for itself. Pausing does not stop a tick
			//that has already entered its locked section (World::Run re-reads the flag
			//inside the lock), and that tick ends in PhysicsSystem::Update writing body
			//poses back over the Transforms. Restoring one entity at a time lets it
			//land between the Transform write and the body teleport, leaving the entity
			//stuck at its simulated pose with a body at the baseline - and, since
			//physics is now paused, nothing ever reconciles the two. Intermittent, and
			//exactly what an unlocked rewind produced in testing.
			std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
			std::string error;
			size_t restored = 0;
			for (const auto& [name, snapshot] : state.physics_preview_baseline) {
				//An entity deleted or cut mid-preview simply has nothing to rewind.
				if (Inspector::RestoreSnapshot(state, name, snapshot, error)) {
					++restored;
				}
			}
			state.status_message = "Physics preview off: rewound " +
				std::to_string(restored) + "/" +
				std::to_string(state.physics_preview_baseline.size()) + " entities";
			state.physics_preview_baseline.clear();
		}

		void SetEnabled(EditorState& state, bool enabled)
		{
			if (state.world == nullptr || enabled == state.physics_preview_active) {
				return;
			}
			state.physics_preview_active = enabled;
			if (enabled) {
				Capture(state);
				state.world->SetPhysicsPause(false);
			}
			else {
				//Pause first: rewinding while the background thread is still stepping
				//would race the restored poses against another physics write-back.
				state.world->SetPhysicsPause(true);
				Rewind(state);
			}
		}

		bool IsEnabled(const EditorState& state)
		{
			return state.physics_preview_active;
		}

		void Reset(EditorState& state)
		{
			state.physics_preview_baseline.clear();
			state.physics_preview_active = false;
			if (state.world != nullptr) {
				state.world->SetPhysicsPause(true);
			}
		}
	}
}
