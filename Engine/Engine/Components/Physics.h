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
#include <Defines.h>
#include <Core/PhysicsCommon.h>
#include <map>

namespace HotBite {
	namespace Engine {
		namespace Components {

			struct Physics {
				enum eShapeForm {
					SHAPE_CAPSULE,
					SHAPE_BOX,
					SHAPE_SPHERE
				};
				reactphysics3d::BodyType type = reactphysics3d::BodyType::STATIC;
				reactphysics3d::RigidBody* body = nullptr;
				reactphysics3d::Collider* collider = nullptr;
				reactphysics3d::Transform last_body_transform;
				reactphysics3d::PhysicsWorld* world = nullptr;
				float bounce = 0.0f;
				eShapeForm shape = eShapeForm::SHAPE_CAPSULE;
				Physics() = default;
				virtual ~Physics();
				Physics& operator=(const Physics& other);
				void SetEnabled(bool enabled);
				bool Init(reactphysics3d::PhysicsWorld* w, reactphysics3d::BodyType body_type,
					Core::ShapeData* shape_data, const float3& extends,
					const float3& p, const float3& s, const float4& r, eShapeForm form = SHAPE_CAPSULE);

				reactphysics3d::Material* GetMaterial();
			};
		}
	}
}