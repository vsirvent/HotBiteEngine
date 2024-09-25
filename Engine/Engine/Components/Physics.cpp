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

#include "Physics.h"
#include <Core/PhysicsCommon.h>
#include <DirectXMath.h>

using namespace reactphysics3d;
using namespace DirectX;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;

std::unordered_map<reactphysics3d::RigidBody*, uint32_t> Physics::mBodyRefs;

bool Physics::Init(PhysicsWorld* w,
	BodyType body_type, ShapeData* shape_data, const float3& extends,
	const float3& p, const float3& s, const float4& r, eShapeForm form) {
	physics_mutex.lock();
	world = w;
	Quaternion q{ r.x, r.y, r.z, r.w };
	Transform t(Vector3{ p.x, p.y, p.z }, q.getUnit());
	body = w->createRigidBody(t);
	this->shape = form;
	this->type = body_type;
	if (body != nullptr) {
		body->setType(body_type);
		mBodyRefs[body]++;
		CollisionShape* cshape = NULL;
		if (shape_data == NULL) {
			float3 e = MULT_F3_F3(extends, s);
			vector4d xm_r = XMVectorSet(r.x, r.y, r.z, r.w);
			vector3d xm_e = XMVectorSet(e.x, e.y, e.z, 1.0f);
			XMStoreFloat3(&e, XMVector3Rotate(xm_e, xm_r));
			e.x = abs(e.x); e.y = abs(e.y); e.z = abs(e.z);
			float radius = (e.x + e.z) / 4.0f;
			switch (form) {
			case eShapeForm::SHAPE_CAPSULE: {
				if (radius > 0.0f) {
					cshape = physics_common.createCapsuleShape(radius, e.y * 2.0f);
					Transform tshape(Vector3{ 0.0f, 0.0f,  radius + e.y }, q.getUnit());
					collider = body->addCollider(cshape, tshape);
				}
			}break;
			case eShapeForm::SHAPE_BOX: {
				cshape = physics_common.createBoxShape({ e.x, e.y, e.z });
				Transform tshape(Vector3{ 0.0f, 0.0f,  0.0f }, q.getUnit());
				collider = body->addCollider(cshape, tshape);
			}break;
			case eShapeForm::SHAPE_SPHERE: {
				if (radius > 0.0f) {
					cshape = physics_common.createSphereShape(e.y);
					Transform tshape(Vector3{ 0.0f, 0.0f, 0.0f }, q.getUnit());
					collider = body->addCollider(cshape, tshape);
				}
			}break;
			}
		}
		else {
			cshape = shape_data->shape;
			collider = body->addCollider(cshape, Transform::identity());
		}
		if (collider != nullptr) {
			Material& material = collider->getMaterial();
			material.setBounciness(0.01f);
			material.setFrictionCoefficient(0.5f);
		}
	}
	if (bounce >= 0.0f) {
		GetMaterial()->setBounciness(bounce);
	}
	if (friction >= 0.0f) {
		GetMaterial()->setFrictionCoefficient(friction);
	}	
	if (air_friction >= 0.0f) {
		body->setLinearDamping(friction);
	}	
	physics_mutex.unlock();
	return (body != nullptr);
}

Physics::~Physics() {
	physics_mutex.lock();
	if (body != nullptr) {
		auto it = mBodyRefs.find(body);
		if (it != mBodyRefs.end()) {
			if (--(it->second) == 0) {
				world->destroyRigidBody(it->first);
				mBodyRefs.erase(it);
			}
		}
		body = nullptr;
	}
	physics_mutex.unlock();
}

Physics::Physics(const Physics& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	mBodyRefs[body]++;
	physics_mutex.unlock();
}

Physics& Physics::operator=(const Physics& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	mBodyRefs[body]++;
	physics_mutex.unlock();
	return *this;
}

void
Physics::SetEnabled(bool enabled) {
	if (body != nullptr) {
		body->setIsActive(enabled);
	}
}

reactphysics3d::Material* Physics::GetMaterial() {
	reactphysics3d::Material* m = nullptr;
	if (collider != nullptr) {
		m = &(collider->getMaterial());
	}
	return m;
}
