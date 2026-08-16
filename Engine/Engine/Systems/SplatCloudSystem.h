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

#include <Components\Base.h>

#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {
			/**
			 * Maintains the transform of a Gaussian splat cloud entity.
			 *
			 * The peer of StaticMeshSystem, and it exists for the same reason that one
			 * excludes entities with a Physics component: Transform::world_matrix has
			 * exactly one owner per entity, and which system that is depends on what the
			 * entity carries.
			 *
			 * A splat cloud has no geometry the engine can measure. Only Base and Transform
			 * are mandatory components, and TemplateOps builds a cloud template out of the
			 * SplatCloud component plus an identity Transform - no Mesh, no Bounds - so
			 * StaticMeshSystem never sees it and, without this, its world matrix would stay
			 * zero-initialized. That is not a subtle error: a zero matrix sends every splat
			 * to the origin with w = 0, so the cloud does not draw in the wrong place, it
			 * does not draw at all.
			 *
			 * Ownership is therefore exclusive both ways. An entity that carries a Mesh and
			 * a Bounds alongside its cloud (legal, if unusual) is left to StaticMeshSystem,
			 * which composes the same matrix and also handles bone attachment - a case this
			 * system cannot reproduce, since the joint pose it would need lives on the
			 * parent's Mesh. An entity with a Physics component is left to PhysicsSystem,
			 * whose bodies write their pose straight into the Transform.
			 *
			 * Bounds are deliberately out of scope. SplatCloudData measures the cloud at
			 * 3 sigma and could fill a Bounds, but nothing culls or picks a splat cloud yet
			 * and a Bounds nothing reads is a component that only has to be kept correct.
			 */
			class SplatCloudSystem : public ECS::System {
			private:
				struct SplatCloudEntity {
					Components::Transform* transform;
					Components::Base* base;
					Components::SplatCloud* cloud;

					SplatCloudEntity(ECS::Coordinator* c, ECS::Entity entity) {
						transform = &(c->GetComponent<Components::Transform>(entity));
						base = &(c->GetComponent<Components::Base>(entity));
						cloud = &(c->GetComponent<Components::SplatCloud>(entity));
					}
				};

				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature signature;
				//What the other two owners of a world matrix claim. Held as signatures so
				//the test is the same bitmask comparison the insertion test is, rather than
				//three ContainsComponent calls per entity per signature change.
				ECS::Signature mesh_signature;
				ECS::Signature physics_signature;
				ECS::EntityVector<SplatCloudEntity> splat_clouds;

				void Update(SplatCloudEntity& entity, int64_t elapsed_nsec, int64_t total_nsec);

			public:
				SplatCloudSystem() = default;
				virtual ~SplatCloudSystem() {}

				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

				//System methods
				void Update(ECS::Entity entity, int64_t elapsed_nsec, int64_t total_nsec);
				void Update(int64_t elapsed_nsec, int64_t total_nsec);
			};
		}
	}
}
