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
#include <Components/Base.h>
#include <Defines.h>
#include <Core/PhysicsCommon.h>
#include <Components/Physics.h>
#include <ECS/Coordinator.h>
#include "PlayerSystem.h"

using namespace reactphysics3d;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;


bool PlayerSystem::LookAt(PlayerData* entity, const float3& dir, float orientation_delta) {
	bool done = false;
	physics_mutex.lock();
	reactphysics3d::Transform t = entity->physics->body->getTransform();
	reactphysics3d::Vector3 p = t.getPosition();
	reactphysics3d::Quaternion q = t.getOrientation();
	vector3d body_pos = XMVectorSet(p.x, p.y, p.z, 0.0f);
	vector3d move_dir = XMVectorSet(-dir.x, dir.y, dir.z, 0.0f);
	vector3d up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	matrix P = XMMatrixLookToLH(body_pos, move_dir, up);
	vector3d move_q = XMQuaternionRotationMatrix(P);

	float4 mq2;
	vector3d rot_q = XMVectorSet(entity->transform->initial_rotation.x, entity->transform->initial_rotation.y,
		entity->transform->initial_rotation.z, entity->transform->initial_rotation.w);
	move_q = XMQuaternionMultiply(rot_q, move_q);
	XMStoreFloat4(&mq2, move_q);

	reactphysics3d::Quaternion qmq2 = { mq2.x, mq2.y, mq2.z, mq2.w };

	float angle = acos(q.dot(qmq2));
	if (angle > 0.1f) {
		q = slerp(q, qmq2, orientation_delta);
	}
	else {
		q = qmq2;
		done = true;
	}
	t.setOrientation(q);
	entity->physics->body->setTransform(t);
	physics_mutex.unlock();
	return done;
}

void PlayerSystem::Move(PlayerData* entity, const float3& offset, float orientation_delta) {
	float amount = 5.0f;	
	physics_mutex.lock();	
	if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) {
		entity->physics->body->setLinearVelocity({ 0.0f, entity->physics->body->getLinearVelocity().y, 0.0f });
		physics_mutex.unlock();
		return;
	}
	reactphysics3d::Transform t = entity->physics->body->getTransform();
	reactphysics3d::Vector3 p = t.getPosition();
	//Small jump to avoid to get stuck
	p.y += 0.01f;
	t.setPosition(p);

	vector3d body_pos = XMVectorSet(p.x, p.y, p.z, 1.0f);
	vector3d move_dir = XMVectorSet(-offset.x, offset.y, offset.z, 1.0f);
	vector3d up = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
	vector3d xaxis = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	matrix P = XMMatrixLookToLH(body_pos, move_dir, up);
	vector3d move_q = XMQuaternionRotationMatrix(P);
	//blender export is rotated in X = -90 deg
	vector3d rot_q = XMQuaternionRotationAxis(xaxis, -XM_PI / 2.0f);
	move_q = XMQuaternionMultiply(rot_q, move_q);
	float4 mq2;
	XMStoreFloat4(&mq2, move_q);
	Quaternion q = t.getOrientation();
	Quaternion qmq2 = { mq2.x, mq2.y, mq2.z, mq2.w };
	float angle = acos(q.dot(qmq2));
	if (angle > 0.05f) {
		q = slerp(q, qmq2, orientation_delta);
		t.setOrientation(q);		
	}
	entity->physics->body->setLinearVelocity({ amount * offset.x, entity->physics->body->getLinearVelocity().y, amount * offset.z });
	entity->physics->body->setTransform(t);
	physics_mutex.unlock();
}

void PlayerSystem::Jump(PlayerData* entity, const float3& offset) {
	physics_mutex.lock();
	entity->physics->body->applyWorldForceAtCenterOfMass({ offset.x, offset.y, offset.z });
	physics_mutex.unlock();
}

