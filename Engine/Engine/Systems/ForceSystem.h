/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

//Physics.h first - see the note at the top of PlatformSystem.h for why the order is
//not cosmetic.
#include <Components/Physics.h>
#include <Components/Base.h>
#include <Components/Force.h>

#include <ECS/Coordinator.h>
#include <ECS/EntityVector.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {

			// Applies every Force component (Components/Force.h) to every dynamic body it
			// reaches.
			//
			// It keeps two lists because the pass is a cross product: each field is tested
			// against each candidate body, so both sides have to be enumerable without
			// walking the whole entity table. The body list holds every entity with a
			// Physics component and the DYNAMIC test happens per tick rather than at
			// insertion - see the note in OnEntitySignatureChanged for why filtering it
			// there is a trap.
			//
			// Runs on the physics thread under Core::physics_mutex, from World::Run, and
			// only while physics is not paused: a force field must not shove the scene
			// around while the Scene Editor is authoring it. Forces are accumulated by
			// reactphysics3d and consumed by the next world step, so the field's effect
			// scales with how many of these ticks fall between two steps - which is a
			// constant for a given pair of thread rates.
			class ForceSystem : public ECS::System {
			private:
				struct ForceEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::Force* force = nullptr;
					//Only Force::TOUCH needs a collider of its own; PROJECTION is pure
					//geometry against the body's position, so this may be null.
					Components::Physics* physics = nullptr;

					ForceEntity(ECS::Coordinator* c, ECS::Entity entity);
				};

				struct BodyEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::Physics* physics = nullptr;

					BodyEntity(ECS::Coordinator* c, ECS::Entity entity);
				};

				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature force_signature;
				ECS::Signature body_signature;
				ECS::EntityVector<ForceEntity> forces;
				ECS::EntityVector<BodyEntity> bodies;

				//The magnitude `force` applies to `body` right now, and 0 when the body is
				//outside the field. Separated out because it is the whole of the component's
				//meaning and the Scene Editor's gizmo draws the same shapes from the same
				//numbers.
				float AppliedForce(const ForceEntity& force, const BodyEntity& body) const;

			public:
				ForceSystem() = default;
				virtual ~ForceSystem() {}

				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

				void Update(int64_t elapsed_nsec, int64_t total_nsec);

				//What the last Update did: how many fields are registered, how many dynamic
				//bodies they were tested against, and how many of those pairs actually
				//pushed. The last is what separates "the field is not set up" from "nothing
				//is standing in it", which a screenshot cannot tell apart.
				struct FrameInfo {
					size_t fields = 0;
					size_t bodies = 0;
					size_t applied = 0;
				};
				const FrameInfo& LastFrame() const { return last_frame; }

				//Where a field is and which way it pushes, with Force::local_dir resolved
				//against the entity's rotation. Both are shared with the editor's gizmo, so
				//the volume it draws and the push a body feels cannot disagree - which they
				//would the moment either side reached for the other spelling of "position".
				//
				//WorldPosition is the composed matrix rather than Transform::position,
				//because for a parented entity that field is an offset in its parent's
				//frame. It falls back to it when the matrix has not been composed at all,
				//which is the ordinary state of an entity carrying neither a Mesh+Bounds
				//nor a Physics component - a bare force field is exactly that, and for such
				//an entity the two are the same thing anyway.
				static float3 WorldDirection(const Components::Force& force,
					const Components::Transform& transform);
				static float3 WorldPosition(const Components::Transform& transform);

			private:
				FrameInfo last_frame;
			};
		}
	}
}
