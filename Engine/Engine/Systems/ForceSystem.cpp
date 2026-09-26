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

#include "ForceSystem.h"
#include "PhysicsSystem.h"

#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

ForceSystem::ForceEntity::ForceEntity(Coordinator* c, Entity entity) {
	base = c->GetComponentPtr<Base>(entity);
	transform = c->GetComponentPtr<Transform>(entity);
	force = c->GetComponentPtr<Force>(entity);
	if (c->ContainsComponent<Physics>(entity)) {
		physics = c->GetComponentPtr<Physics>(entity);
	}
}

ForceSystem::BodyEntity::BodyEntity(Coordinator* c, Entity entity) {
	base = c->GetComponentPtr<Base>(entity);
	transform = c->GetComponentPtr<Transform>(entity);
	physics = c->GetComponentPtr<Physics>(entity);
}

void ForceSystem::OnRegister(Coordinator* c) {
	coordinator = c;
	force_signature.set(c->GetComponentType<Base>(), true);
	force_signature.set(c->GetComponentType<Transform>(), true);
	force_signature.set(c->GetComponentType<Force>(), true);

	body_signature.set(c->GetComponentType<Base>(), true);
	body_signature.set(c->GetComponentType<Transform>(), true);
	body_signature.set(c->GetComponentType<Physics>(), true);
}

void ForceSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & force_signature) == force_signature) {
		forces.Insert(entity, { coordinator, entity });
	}
	else {
		forces.Remove(entity);
	}
	//Every entity with a body, DYNAMIC or not: the type is re-checked in Update instead of
	//here. It looks wasteful - a force can only move a dynamic body, so the rest are
	//iterated for nothing - and filtering here is what the Marbles original did, but the
	//type and the rigid body both change *without* a signature change to announce it. A
	//body is created after its component exists (World::Init seats the ones a level's
	//component blocks created), an editor edit rebuilds it, and a Platform's fall_delay
	//promotes a KINEMATIC body to DYNAMIC with nothing notified at all. Anything filtered
	//out at insert time by a value that can change later stays filtered out forever, and
	//what that looks like is a force field that silently does nothing to one object.
	//
	//The cost is a cheap `continue` per pair, and the whole pass is skipped when no field
	//is registered - which is every scene that has none.
	//
	//The components must be reached only once the signature says they are there:
	//Coordinator::GetComponentPtr throws on a component the entity does not have, which is
	//exactly the case this method is called for when one has just been removed.
	if ((entity_signature & body_signature) == body_signature) {
		bodies.Insert(entity, { coordinator, entity });
	}
	else {
		bodies.Remove(entity);
	}
}

void ForceSystem::OnEntityDestroyed(Entity entity) {
	forces.Remove(entity);
	bodies.Remove(entity);
}

float3 ForceSystem::WorldDirection(const Force& force, const Transform& transform) {
	if (!force.local_dir) {
		return force.dir;
	}
	vector3d q = XMVectorSet(transform.rotation.x, transform.rotation.y,
		transform.rotation.z, transform.rotation.w);
	vector3d d = XMVector3Rotate(XMVectorSet(force.dir.x, force.dir.y, force.dir.z, 0.0f), q);
	return float3{ XMVectorGetX(d), XMVectorGetY(d), XMVectorGetZ(d) };
}

float3 ForceSystem::WorldPosition(const Transform& transform) {
	//w == 1 is what says the matrix has been composed at all: an entity whose transform
	//system has not run - or has none, which a bare force field does not - still holds the
	//zero matrix it was constructed with, and the origin is not a sensible default for a
	//force field.
	if (XMVectorGetW(transform.world_xmmatrix.r[3]) == 1.0f) {
		return float3{ XMVectorGetX(transform.world_xmmatrix.r[3]),
			XMVectorGetY(transform.world_xmmatrix.r[3]),
			XMVectorGetZ(transform.world_xmmatrix.r[3]) };
	}
	return transform.position;
}

float ForceSystem::AppliedForce(const ForceEntity& f, const BodyEntity& b) const {
	switch (f.force->type) {
	case Force::PROJECTION: {
		const float3 dir = WorldDirection(*f.force, *f.transform);
		if (LENGHT_SQUARE_F3(dir) <= 0.0f || f.force->range <= 0.0f) {
			return 0.0f;
		}
		const float3 origin = WorldPosition(*f.transform);
		//The body's pose comes from its rigid body rather than its Transform: this runs on
		//the physics thread and PhysicsSystem copies the one into the other on the
		//background thread, so the Transform is up to a tick behind - and for a parented
		//body it is an offset in its parent's frame rather than a place in the world.
		const reactphysics3d::Vector3& bp = b.physics->body->getTransform().getPosition();
		const float3 at{ bp.x, bp.y, bp.z };
		//`range` is a distance from the field's own position, not a length along the
		//beam's axis: a body just outside the far end of the cylinder but off to one
		//side is outside the field, which is what the radius test below then confirms.
		const float distance = LENGHT_F3(SUB_F3_F3(at, origin));
		if (distance >= f.force->range) {
			return 0.0f;
		}
		const float3 end = ADD_F3_F3(origin, MULT_F3_F(UNIT_F3(dir), f.force->range));
		if (distance_point_line(at, origin, end) > f.force->radius) {
			return 0.0f;
		}
		//Linear ramp from `origin_force` at the field to `force` at its reach. See
		//Force::origin_force for why the default is the weak end nearest the fan.
		const float t = distance / f.force->range;
		return f.force->origin_force + (f.force->force - f.force->origin_force) * t;
	}
	case Force::TOUCH: {
		if (f.physics == nullptr || f.physics->body == nullptr) {
			return 0.0f;
		}
		auto physics_system = coordinator->GetSystem<PhysicsSystem>();
		if (physics_system == nullptr ||
			!physics_system->IsContact(b.base->id, f.base->id)) {
			return 0.0f;
		}
		return f.force->force;
	}
	default:
		return 0.0f;
	}
}

void ForceSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	last_frame = FrameInfo();
	last_frame.fields = forces.GetConstData().size();
	last_frame.bodies = bodies.GetConstData().size();
	if (forces.GetData().empty() || bodies.GetData().empty()) {
		return;
	}
	for (ForceEntity& f : forces.GetData()) {
		if (f.force->type == Force::NONE) {
			continue;
		}
		//Resolved once per field rather than once per pair: it costs a quaternion rotate
		//and every body of the inner loop sees the same answer.
		const float3 dir = WorldDirection(*f.force, *f.transform);
		if (LENGHT_SQUARE_F3(dir) <= 0.0f) {
			continue;
		}
		const float3 unit = UNIT_F3(dir);
		for (BodyEntity& b : bodies.GetData()) {
			//A field never pushes itself, however it is shaped. And only a DYNAMIC body
			//can be pushed at all - reactphysics3d discards a force on any other kind, so
			//testing one is work that cannot have a result.
			if (b.base->id == f.base->id || b.physics->body == nullptr ||
				b.physics->type != reactphysics3d::BodyType::DYNAMIC) {
				continue;
			}
			const float magnitude = AppliedForce(f, b);
			if (magnitude == 0.0f) {
				continue;
			}
			const float3 push = MULT_F3_F(unit, magnitude);
			b.physics->body->applyWorldForceAtCenterOfMass({ push.x, push.y, push.z });
			++last_frame.applied;
		}
	}
}
