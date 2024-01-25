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

#include <Components/Physics.h>
#include "PhysicsSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

void PhysicsSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<Bounds>(), true);
	signature.set(coordinator->GetComponentType<Physics>(), true);
}

void PhysicsSystem::OnEntityDestroyed(Entity entity) {
	AutoLock l(lock);
	for (auto it = entity_by_body.begin(); it != entity_by_body.end(); ++it) {
		if (it->second != nullptr && it->second->base->id == entity) {
			entity_by_body.erase(it);
			break;
		}
	}
	physics.Remove(entity);
}

void PhysicsSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	AutoLock l(lock);
	if ((entity_signature & signature) == signature)
	{		
		PhysicsEntity pe{ coordinator, entity };
		physics.Insert(entity, pe);
		entity_by_body[pe.physics->body] = physics.Get(entity);
	}
	else
	{
		PhysicsEntity* pe = physics.Get(entity);
		if (pe != nullptr) {
			entity_by_body.erase(pe->physics->body);
		}
		physics.Remove(entity);		
	}
}

void PhysicsSystem::Init(reactphysics3d::PhysicsWorld* w) {
	this->world = w;
	world->setEventListener(this);
}

bool PhysicsSystem::IsContact(ECS::Entity entity1, ECS::Entity entity2) const {
	bool ret = false;
	const PhysicsEntity* e1 = physics.Get(entity1);
	if (e1 == nullptr) { return ret; }
	const PhysicsEntity* e2 = physics.Get(entity2);
	if (e2 == nullptr) { return ret; }
	auto it1 = contacts_by_body.find(e1->physics->body);
	if (it1 != contacts_by_body.end()) {
		auto it2 = it1->second.find(e2->physics->body);
		if (it2 != it1->second.end()) {
			ret = true;
		}
	}
	return ret;
}

void PhysicsSystem::ClearContact(ECS::Entity entity) {
	const PhysicsEntity* e = physics.Get(entity);
	if (e != nullptr) {
		auto it = contacts_by_body.find(e->physics->body);
		if (it != contacts_by_body.end()) {
			contacts_by_body.erase(it);
		}
		for (auto& [key, value] : contacts_by_body) {
			auto it2 = value.find(e->physics->body);
			if (it2 != value.end()) {
				value.erase(it2);
			}
		}
	}
}

void PhysicsSystem::onTrigger(const reactphysics3d::OverlapCallback::CallbackData& callbackData) {
	// For each contact pair
	for (uint32_t p = 0; p < callbackData.getNbOverlappingPairs(); ++p) {
		const reactphysics3d::OverlapCallback::OverlapPair& contactPair = callbackData.getOverlappingPair(p);
		if (contactPair.getEventType() == reactphysics3d::OverlapCallback::OverlapPair::EventType::OverlapExit) {
			auto it1 = contacts_by_body.find(contactPair.getBody1());
			if (it1 != contacts_by_body.end()) {
				auto it2 = it1->second.find(contactPair.getBody2());
				if (it2 != it1->second.end()) {
					it1->second.erase(it2);
					if (it1->second.empty()) {
						contacts_by_body.erase(it1);
					}
				}
			}
			it1 = contacts_by_body.find(contactPair.getBody2());
			if (it1 != contacts_by_body.end()) {
				auto it2 = it1->second.find(contactPair.getBody1());
				if (it2 != it1->second.end()) {
					it1->second.erase(it2);
					if (it1->second.empty()) {
						contacts_by_body.erase(it1);
					}
				}
			}
		}
		else {
			//Create empty entries
			auto* pair0 = &(contacts_by_body[contactPair.getBody1()][contactPair.getBody2()]);
			auto* pair1 = &(contacts_by_body[contactPair.getBody2()][contactPair.getBody1()]);
		}
	}
}

void PhysicsSystem::onContact(const reactphysics3d::CollisionCallback::CallbackData& callbackData) {
	// For each contact pair
	AutoLock l(lock);
	for (uint32_t p = 0; p < callbackData.getNbContactPairs(); ++p) {
		const ContactPair& contactPair = callbackData.getContactPair(p);
		if (contactPair.getEventType() == ContactPair::EventType::ContactExit) {
			auto it1 = contacts_by_body.find(contactPair.getBody1());
			if (it1 != contacts_by_body.end()) {
				auto it2 = it1->second.find(contactPair.getBody2());
				if (it2 != it1->second.end()) {
					it1->second.erase(it2);
					if (it1->second.empty()) {
						contacts_by_body.erase(it1);
					}
				}
			}
			it1 = contacts_by_body.find(contactPair.getBody2());
			if (it1 != contacts_by_body.end()) {
				auto it2 = it1->second.find(contactPair.getBody1());
				if (it2 != it1->second.end()) {
					it1->second.erase(it2);
					if (it1->second.empty()) {
						contacts_by_body.erase(it1);
					}
				}
			}
			
		} else {
			auto* pair0 = &(contacts_by_body[contactPair.getBody1()][contactPair.getBody2()]);
			auto* pair1 = &(contacts_by_body[contactPair.getBody2()][contactPair.getBody1()]);
			pair0->clear();
			pair1->clear();
			for (uint32_t c = 0; c < contactPair.getNbContactPoints(); ++c) {
				pair0->insert(contactPair.getContactPoint(c).getLocalPointOnCollider1());
				pair1->insert(contactPair.getContactPoint(c).getLocalPointOnCollider1());
			}			
		}
		if (contactPair.getEventType() == ContactPair::EventType::ContactStart || contactPair.getEventType() == ContactPair::EventType::ContactExit) {
			const auto b1 = entity_by_body.find(contactPair.getBody1());
			const auto b2 = entity_by_body.find(contactPair.getBody2());
			const auto end = entity_by_body.cend();
			if (b1 != end && b2 != end) {
				Event ev;
				switch (contactPair.getEventType()) {
				case ContactPair::EventType::ContactStart:
					ev.SetType(EVENT_ID_COLLISION_START);
					break;
				case ContactPair::EventType::ContactExit:
					ev.SetType(EVENT_ID_COLLISION_END);
					break;
				}
				int64_t now = Scheduler::GetNanoSeconds();
				ev.SetParam(EVENT_PARAM_ENTITY_1, b1->second->base->id);
				ev.SetParam(EVENT_PARAM_ENTITY_2, b2->second->base->id);
				if ((now - last_event_by_entity[b1->second->base->id][(int)contactPair.getEventType()]) > MSEC_TO_NSEC(10)) {
					ev.SetEntity(b1->second->base->id);
					coordinator->SendEvent(ev);
					last_event_by_entity[b1->second->base->id][(int)contactPair.getEventType()] = now;
				}
				if ((now - last_event_by_entity[b2->second->base->id][(int)contactPair.getEventType()]) > MSEC_TO_NSEC(10)) {
					ev.SetEntity(b2->second->base->id);
					coordinator->SendEvent(ev);
					last_event_by_entity[b2->second->base->id][(int)contactPair.getEventType()] = now;
				}				
			}		
		}
	}
}

void PhysicsSystem::Update(PhysicsEntity& pe, int64_t elapsed_nsec, int64_t total_nsec, bool force) {

	
	Transform* transform = pe.transform;
	Bounds* bounds = pe.bounds;
	Physics* physics = pe.physics;
	Base* base = pe.base;

	const reactphysics3d::Transform& bt = physics->body->getTransform();

	if (physics->type != reactphysics3d::BodyType::STATIC && (force || physics->last_body_transform != bt)) {
		const reactphysics3d::Vector3& p = bt.getPosition();
		const reactphysics3d::Quaternion& q = bt.getOrientation();
		XMMATRIX t = XMMatrixTranslation(p.x, p.y, p.z);
		XMMATRIX r = XMMatrixRotationQuaternion({ q.x, q.y, q.z, q.w });
		XMMATRIX s = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z);
		transform->position.x = p.x;
		transform->position.y = p.y;
		transform->position.z = p.z;
		transform->rotation.x = q.x;
		transform->rotation.y = q.y;
		transform->rotation.z = q.z;
		transform->rotation.w = q.w;

		transform->world_xmmatrix = s * r * t;
		XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(transform->world_xmmatrix));
		XMStoreFloat4x4(&transform->world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform->world_xmmatrix)));

		vector3d pos = XMVectorAdd(XMVector4Transform(XMLoadFloat3(&bounds->bounding_box.Center), transform->world_xmmatrix), XMLoadFloat3(&transform->position));


		bounds->final_box = bounds->bounding_box;
		XMStoreFloat3(&bounds->final_box.Center, pos);
		bounds->final_box.Extents = MULT_F3_F3(bounds->final_box.Extents, transform->scale);
		bounds->final_box.Orientation = transform->rotation;
		physics->last_body_transform = bt;
		coordinator->SendEvent(this, pe.base->id, Transform::EVENT_ID_TRANSFORM_CHANGED);
	}	
}

reactphysics3d::PhysicsWorld*
PhysicsSystem::GetWorld() {
	return world;
}

void PhysicsSystem::Update(int64_t elapsed_nsec, int64_t total_nsec, bool force) {
	for (auto it = physics.GetData().begin(); it != physics.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec, force);
	}
}