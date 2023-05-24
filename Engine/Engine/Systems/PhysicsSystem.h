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

#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>
#include <Components\Base.h>
#include <Components\Physics.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {
			class PhysicsSystem: public ECS::System, public reactphysics3d::EventListener {
			public:
				using CollisionData = std::unordered_map<reactphysics3d::CollisionBody* /*body 1*/,
					std::unordered_map<reactphysics3d::CollisionBody* /* body 2*/, std::set<reactphysics3d::Vector3> /* collision points */>>;
				
			private:
				struct PhysicsEntity {
					Components::Transform* transform;
					Components::Bounds* bounds;
					Components::Physics* physics;
					Components::Base* base;
					PhysicsEntity(ECS::Coordinator* c, ECS::Entity entity) {
						transform = &(c->GetComponent<Components::Transform>(entity));
						base = &(c->GetComponent<Components::Base>(entity));
						bounds = &(c->GetComponent<Components::Bounds>(entity));
						physics = &(c->GetComponent<Components::Physics>(entity));
					}
				};

				reactphysics3d::PhysicsWorld* world = nullptr;
				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature signature;
				ECS::EntityVector<PhysicsEntity> physics;
				CollisionData contacts_by_body;

			public:
				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;
				void Update(PhysicsEntity& pe, int64_t elapsed_nsec, int64_t total_nsec, bool force);
				//reactphysics3d::CollisionCallback implementation
				void onContact(const reactphysics3d::CollisionCallback::CallbackData& callbackData) override;
				void onTrigger(const reactphysics3d::OverlapCallback::CallbackData& callbackData) override;
			public:

				PhysicsSystem() = default;
				virtual ~PhysicsSystem() {
					world->setEventListener(nullptr);
				}
				
				//System methods
				void Init(reactphysics3d::PhysicsWorld* w);
				void Update(int64_t elapsed_nsec, int64_t total_nsec, bool force);
				reactphysics3d::PhysicsWorld* GetWorld();
				bool IsContact(ECS::Entity entity1, ECS::Entity entity2) const;
				void ClearContact(ECS::Entity entity);
				const CollisionData&
					GetCollisions() {
					return contacts_by_body;
				}			
			};
		}
	}
}

