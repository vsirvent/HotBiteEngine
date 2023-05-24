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
#include <Components\Particles.h>
#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {
			class ParticleSystem : public ECS::System {
			public:
				struct ParticleEntity {
					Components::Base* base;
					Components::Transform* transform;
					Components::Particles* particles;
					ParticleEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						particles = &(c->GetComponent<Components::Particles>(entity));
					}
				};
				ECS::Signature particles_signature;

				ECS::Coordinator* coordinator = nullptr;
				ECS::EntityVector<ParticleEntity> particles;

			public:
				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

			public:
				ParticleSystem() = default;
				virtual ~ParticleSystem() {}
				//System methods
				void Update(int64_t elapsed_nsec, int64_t total_nsec);
			};
		}
	}
}

