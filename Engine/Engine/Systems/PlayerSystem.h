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

#include <reactphysics3d\reactphysics3d.h>
#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>
#include <Components\Base.h>
#include <Components\Physics.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {
			//Abstract system, this is base system that can be used for creating a player system
			class PlayerSystem : public ECS::System {
			public:
				struct PlayerData {
					Components::Base* base;
					Components::Transform* transform;
					Components::Physics* physics;
					Components::Mesh* mesh;

					PlayerData(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						mesh = &(c->GetComponent<Components::Mesh>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						physics = &(c->GetComponent<Components::Physics>(entity));
					}

					static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
						HotBite::Engine::ECS::Signature signature;
						signature.set(c->GetComponentType<HotBite::Engine::Components::Base>(), true);
						signature.set(c->GetComponentType<HotBite::Engine::Components::Mesh>(), true);
						signature.set(c->GetComponentType<HotBite::Engine::Components::Transform>(), true);
						signature.set(c->GetComponentType<HotBite::Engine::Components::Physics>(), true);
						return signature;
					}

				};
		
			public:
				PlayerSystem() = default;
				virtual ~PlayerSystem() {}

				//Player methods
				virtual bool LookAt(PlayerData* entity, const float3& dir, float orientation_delta = 0.1f);
				virtual void Move(PlayerData* entity, const float3& offset, float orientation_delta = 0.1f);
				virtual void Jump(PlayerData* entity, const float3& offset);
			};
		}
	}
}


