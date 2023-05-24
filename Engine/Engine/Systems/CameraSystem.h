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
#include <Components\Camera.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {

			class CameraSystem : public ECS::System {
			public:

				static inline ECS::EventId EVENT_ID_CAMERA_MOVED = ECS::GetEventId<CameraSystem>(0x00);
				static inline ECS::ParamId EVENT_PARAM_CAMERA_DATA = 0x00;

				struct CameraData {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::Camera* camera = nullptr;

					CameraData() = default;

					CameraData(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						camera = &(c->GetComponent<Components::Camera>(entity));
					}

					static ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
						ECS::Signature signature;
						signature.set(c->GetComponentType<Components::Base>(), true);
						signature.set(c->GetComponentType<Components::Transform>(), true);
						signature.set(c->GetComponentType<Components::Camera>(), true);
						return signature;
					}
				};
			private:

				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature signature;
				ECS::EntityVector<CameraData> cameras;

			public:
				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

			public:
				CameraSystem() = default;
				virtual ~CameraSystem();

				//Camera methods
				bool Init(CameraData& entity);

				void SetPosition(CameraData& entity, float3 pos);
				void SetRotation(CameraData& entity, float3 rot);
				void RotateX(CameraData& entity, float x);
				void RotateY(CameraData& entity, float y);
				void RotateZ(CameraData& entity, float z);
				void Zoom(CameraData& entity, float amount);

				void Update(CameraData& entity, int64_t elapsed_nsec, int64_t total_nsec);
				//System methods
				void Update(int64_t elapsed_nsec, int64_t total_nsec);
				ECS::EntityVector<CameraData>& GetCameras();
			};
		}
	}
}
