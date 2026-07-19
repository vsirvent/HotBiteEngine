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
#include <reactphysics3d\body\CollisionBody.h>
#include <Defines.h>
#include <Core/PhysicsCommon.h>
#include <map>

namespace HotBite {
	namespace Engine {
		namespace Components {

			struct Physics {
				enum eShapeForm {
					SHAPE_NONE,
					SHAPE_CAPSULE,
					SHAPE_BOX,
					SHAPE_SPHERE,
				};
				static std::unordered_map<reactphysics3d::RigidBody*, uint32_t> mBodyRefs;
				reactphysics3d::BodyType type = reactphysics3d::BodyType::STATIC;
				reactphysics3d::RigidBody* body = nullptr;
				reactphysics3d::Collider* collider = nullptr;
				//The collision shape behind `collider` when this component created it
				//(the primitive forms, or the privately scaled mesh shape), as opposed
				//to a ShapeData shape shared with every other entity on the same mesh.
				//Only an owned shape may be destroyed when the collider is replaced.
				reactphysics3d::CollisionShape* owned_shape = nullptr;

				// A private, pre-scaled copy of a mesh collider's geometry, built when
				// this entity needs the shape at a scale other than the one baked into
				// the shared ShapeData.
				//
				// Why the scale is baked in rather than passed to
				// createConcaveMeshShape's `scaling` argument: reactphysics3d only
				// applies that scale in the NARROW phase. ConcaveMeshShape::getLocalBounds
				// returns the raw (unscaled) BVH bounds, so the collider's broad-phase
				// AABB keeps the unscaled size and anything landing outside it is never
				// paired for a narrow-phase test at all - a ball drops straight through
				// a scaled block. Baking the scale into the vertices keeps the shape at
				// scale 1, which is the only configuration the library handles
				// consistently (and the one every unscaled collider already uses).
				struct ScaledMesh;
				ScaledMesh* owned_mesh = nullptr;
				reactphysics3d::Transform last_body_transform;
				reactphysics3d::PhysicsWorld* world = nullptr;
				float bounce = -1.0f;
				float friction = -1.0f;
				float air_friction = -1.0f;
				eShapeForm shape = eShapeForm::SHAPE_CAPSULE;
				Physics() = default;
				Physics(const Physics& other);
				Physics(Physics&& other);
				virtual ~Physics();
				Physics& operator=(const Physics& other);
				Physics& operator=(Physics&& other);
				void SetEnabled(bool enabled);
				bool Init(reactphysics3d::PhysicsWorld* w, reactphysics3d::BodyType body_type,
					Core::ShapeData* shape_data, const float3& extends,
					const float3& p, const float3& s, const float4& r, eShapeForm form = SHAPE_CAPSULE);

				// Rebuilds the collider of an already-initialized body for a new
				// scale/rotation, keeping the body (and its refcount) alive. The
				// collision shape is derived from the Transform at Init time, so
				// without this a scaled or rotated entity keeps the collider it was
				// born with. Editors must call it after changing Transform.scale or
				// Transform.rotation; a pure translation only needs
				// RigidBody::setTransform. `shape_data` must be the one Init received
				// (null for the primitive capsule/box/sphere forms). Returns false
				// when there is no body to update.
				bool UpdateShape(Core::ShapeData* shape_data, const float3& extends,
					const float3& s, const float4& r);

				reactphysics3d::Material* GetMaterial();

				// The triangles a mesh collider actually collides with, when this
				// component built its own pre-scaled copy of them. Null means it shares
				// the ShapeData geometry unscaled. Debug overlays should ask here
				// rather than assume the ShapeData is what is in the physics world -
				// the point of drawing a collider is to catch exactly that kind of
				// disagreement.
				const std::vector<float3>* GetOwnedMeshVertices() const;
				const std::vector<unsigned int>* GetOwnedMeshIndices() const;

			private:
				// Creates and attaches the collision shape for `s`/`r`. Caller holds
				// physics_mutex, `body` is valid and carries no collider.
				void AddCollider(Core::ShapeData* shape_data, const float3& extends,
					const float3& s, const float4& r);
			};
		}
	}
}