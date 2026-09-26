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

#include "PlatformSystem.h"
#include "PhysicsSystem.h"

#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace {

	//How far a platform has to be from where the system last put it before the system
	//concludes that something else moved it. One millimetre squared: a kinematic body
	//written with setTransform reads back exactly, so this only has to clear
	//floating-point noise, and it has to stay well under any authored motion.
	constexpr float RELATCH_EPSILON_SQ = 1e-6f;

	//The pose the platform's owner holds. A platform with a rigid body is moved through
	//that body (PhysicsSystem::Update copies it back into the Transform afterwards), so
	//the body is where the truth is; one without is moved through its Transform.
	float3 CurrentPosition(const Transform* transform, const Physics* physics) {
		if (physics != nullptr && physics->body != nullptr) {
			const reactphysics3d::Vector3& p = physics->body->getTransform().getPosition();
			return float3{ p.x, p.y, p.z };
		}
		return transform->position;
	}

	//The rotation half of the same question. Only a spinning platform asks it.
	float4 CurrentRotation(const Transform* transform, const Physics* physics) {
		if (physics != nullptr && physics->body != nullptr) {
			const reactphysics3d::Quaternion& q = physics->body->getTransform().getOrientation();
			return float4{ q.x, q.y, q.z, q.w };
		}
		return transform->rotation;
	}

	//Whether two quaternions differ enough to have been set by something other than this
	//system. Component-wise, not an angle: the system writes the value itself, so the two
	//are bit-identical when nobody else has touched it and the sign ambiguity between q
	//and -q never arises.
	bool RotationChanged(const float4& a, const float4& b) {
		const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z, dw = a.w - b.w;
		return (dx * dx + dy * dy + dz * dz + dw * dw) > 1e-8f;
	}

	//A platform that cannot report its motion is not a platform. See the note in
	//PlatformSystem.h: both of these are silent, and neither looks like its own cause.
	//
	//Called at the point the platform is about to be moved, not when it is first seen, and
	//deliberately: a Platform or LinearPlatform sitting at its defaults is inert, and
	//promoting a body type and taking a caster out of the static shadow map are real
	//changes to make to a piece of scenery that is not going anywhere. Idempotent and two
	//comparisons wide, so calling it per tick costs nothing.
	void EnsureMovable(Base* base, Physics* physics) {
		if (physics != nullptr && physics->body != nullptr &&
			physics->type == reactphysics3d::BodyType::STATIC) {
			physics->body->setType(reactphysics3d::BodyType::KINEMATIC);
			physics->type = reactphysics3d::BodyType::KINEMATIC;
		}
		if (base->is_static) {
			base->is_static = false;
		}
	}

	//Writes the platform's new pose. `rotation` is null when the platform does not spin,
	//which is not the same as passing the identity: the authored rotation must be left
	//exactly as it is rather than replaced by a quaternion built from it.
	void ApplyPose(Transform* transform, Physics* physics, const float3& position,
		const float4* rotation) {
		if (physics != nullptr && physics->body != nullptr) {
			reactphysics3d::Transform t = physics->body->getTransform();
			t.setPosition({ position.x, position.y, position.z });
			if (rotation != nullptr) {
				t.setOrientation({ rotation->x, rotation->y, rotation->z, rotation->w });
			}
			physics->body->setTransform(t);
			return;
		}
		transform->position = position;
		if (rotation != nullptr) {
			transform->rotation = *rotation;
		}
		transform->dirty = true;
	}

	//Displaces everything standing on the platform by the platform's own motion: a
	//translation for every rider, plus an orbit about `axis` through `pivot` when the
	//platform turned. reactphysics3d does none of this itself - a kinematic body imparts
	//no friction - so without it a ball sits still while the lift rises out from under
	//it.
	void CarryRiders(Coordinator* coordinator, Base* base, Physics* physics,
		const float3& delta, const float3& pivot, const float3& axis, float spin_delta) {
		//Riders are contacts, which only exist for a platform with a collider.
		if (physics == nullptr || physics->body == nullptr) {
			return;
		}
		auto physics_system = coordinator->GetSystem<PhysicsSystem>();
		if (physics_system == nullptr) {
			return;
		}
		matrix spin{};
		const bool turning = (spin_delta != 0.0f);
		vector3d pivot_v{};
		if (turning) {
			spin = XMMatrixRotationAxis(
				XMVector3Normalize(XMVectorSet(axis.x, axis.y, axis.z, 0.0f)), spin_delta);
			pivot_v = XMVectorSet(pivot.x, pivot.y, pivot.z, 0.0f);
		}
		for (Entity rider : physics_system->GetContacts(base->id)) {
			if (rider == base->id || !coordinator->ContainsComponent<Physics>(rider)) {
				continue;
			}
			Physics& rp = coordinator->GetComponent<Physics>(rider);
			//Only what the simulation owns. A kinematic or static body in contact belongs
			//to something else - the level, or another platform - and pushing it would
			//leave one pose with two writers.
			if (rp.body == nullptr || rp.type != reactphysics3d::BodyType::DYNAMIC) {
				continue;
			}
			reactphysics3d::Transform rt = rp.body->getTransform();
			const reactphysics3d::Vector3& p = rt.getPosition();
			float3 position{ p.x, p.y, p.z };
			if (turning) {
				vector3d offset = XMVectorSubtract(
					XMVectorSet(position.x, position.y, position.z, 0.0f), pivot_v);
				offset = XMVectorAdd(XMVector3TransformNormal(offset, spin), pivot_v);
				position = float3{ XMVectorGetX(offset), XMVectorGetY(offset), XMVectorGetZ(offset) };
			}
			position = ADD_F3_F3(position, delta);
			rt.setPosition({ position.x, position.y, position.z });
			rp.body->setTransform(rt);
		}
	}
}

// ===========================================================================
// PlatformSystem
// ===========================================================================

PlatformSystem::PlatformEntity::PlatformEntity(Coordinator* c, Entity entity) {
	base = c->GetComponentPtr<Base>(entity);
	transform = c->GetComponentPtr<Transform>(entity);
	platform = c->GetComponentPtr<Platform>(entity);
	//Optional, and checked rather than assumed: GetComponentPtr throws on a component
	//the entity does not have.
	if (c->ContainsComponent<Physics>(entity)) {
		physics = c->GetComponentPtr<Physics>(entity);
	}
}

void PlatformSystem::OnRegister(Coordinator* c) {
	coordinator = c;
	signature.set(c->GetComponentType<Base>(), true);
	signature.set(c->GetComponentType<Transform>(), true);
	signature.set(c->GetComponentType<Platform>(), true);
}

void PlatformSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & signature) == signature) {
		platforms.Insert(entity, { coordinator, entity });
	}
	else {
		platforms.Remove(entity);
	}
}

void PlatformSystem::OnEntityDestroyed(Entity entity) {
	platforms.Remove(entity);
}

void PlatformSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	const float elapsed = (float)elapsed_nsec / 1000000000.0f;
	for (PlatformEntity& e : platforms.GetData()) {
		Update(e, elapsed);
	}
}

void PlatformSystem::Update(PlatformEntity& e, float elapsed_seconds) {
	Platform* p = e.platform;
	Platform::Runtime& rt = p->rt;

	//A fallen platform belongs to the simulation now, and nothing here may touch it -
	//including the re-latch below, which would otherwise chase it down.
	if (rt.fallen) {
		return;
	}

	const bool spins = (p->angular_speed != 0.0f && LENGHT_SQUARE_F3(p->angular_dir) > 0.0f);
	const float3 current = CurrentPosition(e.transform, e.physics);
	const float4 current_rotation = CurrentRotation(e.transform, e.physics);
	//Somebody else moved the platform: a gizmo drag, an undo, the physics preview's
	//rewind. Measure from where it is now - swinging about a centre the platform has been
	//dragged away from is how authoring a moving platform in the editor walks it a little
	//further off with every edit. The rotation only counts for a platform that spins,
	//since that is the only case this system writes one: otherwise a pure rotation edit
	//would restart an oscillation it has nothing to do with.
	const bool moved_externally = rt.latched &&
		(LENGHT_SQUARE_F3(SUB_F3_F3(current, rt.last_applied)) > RELATCH_EPSILON_SQ ||
			(spins && RotationChanged(current_rotation, rt.last_applied_rotation)));
	if (!rt.latched || moved_externally) {
		rt.centre = current;
		rt.last_applied = current;
		rt.base_rotation = current_rotation;
		rt.last_applied_rotation = current_rotation;
		rt.clock = 0.0f;
		rt.latched = true;
	}

	rt.clock += elapsed_seconds;
	const float motion_time = rt.clock - p->delay;
	if (motion_time < 0.0f) {
		return;
	}
	//How much of this tick the platform has actually been moving for, which is the whole
	//tick except on the one where the delay ran out.
	const float step_seconds = (std::min)(elapsed_seconds, motion_time);

	if (p->fall_delay >= 0.0f && motion_time >= p->fall_delay &&
		e.physics != nullptr && e.physics->body != nullptr) {
		e.physics->body->setType(reactphysics3d::BodyType::DYNAMIC);
		e.physics->type = reactphysics3d::BodyType::DYNAMIC;
		rt.fallen = true;
		return;
	}

	float3 target = rt.centre;
	//UNIT_F3 divides by the length with no guard of its own, so a zero direction would
	//send the platform to NaN and take every rider standing on it along.
	const bool oscillating = (p->amplitude != 0.0f && LENGHT_SQUARE_F3(p->linear_dir) > 0.0f);
	if (oscillating) {
		const float angle = XM_2PI * (p->freq * motion_time + p->phase);
		target = ADD_F3_F3(rt.centre,
			MULT_F3_F(UNIT_F3(p->linear_dir), p->amplitude * std::sinf(angle)));
	}

	float4 rotation{};
	const float4* rotation_ptr = nullptr;
	float spin_delta = 0.0f;
	if (spins) {
		spin_delta = p->angular_speed * step_seconds;
		//The total angle comes from the clock rather than from an accumulator, for the
		//same reason the position does: an accumulated angle is tick-rate dependent and
		//drifts. Wrapped, or the precision of the product falls away over a long session.
		const float total = std::fmodf(p->angular_speed * motion_time, XM_2PI);
		vector3d spin = XMQuaternionRotationMatrix(XMMatrixRotationAxis(
			XMVector3Normalize(XMVectorSet(p->angular_dir.x, p->angular_dir.y, p->angular_dir.z, 0.0f)),
			total));
		//The latched base, never Transform::initial_rotation - see Platform::Runtime.
		vector3d base_q = XMVectorSet(rt.base_rotation.x, rt.base_rotation.y,
			rt.base_rotation.z, rt.base_rotation.w);
		XMStoreFloat4(&rotation, XMQuaternionMultiply(base_q, spin));
		rotation_ptr = &rotation;
	}

	const float3 delta = SUB_F3_F3(target, rt.last_applied);
	if (rotation_ptr == nullptr && LENGHT_SQUARE_F3(delta) <= 0.0f) {
		//Nothing authored, or nothing moved this tick. Leave the entity entirely alone
		//rather than rewriting the pose it already has - a Transform marked dirty every
		//tick makes StaticMeshSystem recompose a world matrix that did not change.
		return;
	}

	EnsureMovable(e.base, e.physics);
	ApplyPose(e.transform, e.physics, target, rotation_ptr);
	CarryRiders(coordinator, e.base, e.physics, delta, target, p->angular_dir, spin_delta);
	rt.last_applied = target;
	if (rotation_ptr != nullptr) {
		rt.last_applied_rotation = rotation;
	}
}

// ===========================================================================
// LinearPlatformSystem
// ===========================================================================

LinearPlatformSystem::LinearPlatformEntity::LinearPlatformEntity(Coordinator* c, Entity entity) {
	base = c->GetComponentPtr<Base>(entity);
	transform = c->GetComponentPtr<Transform>(entity);
	platform = c->GetComponentPtr<LinearPlatform>(entity);
	if (c->ContainsComponent<Physics>(entity)) {
		physics = c->GetComponentPtr<Physics>(entity);
	}
}

void LinearPlatformSystem::OnRegister(Coordinator* c) {
	coordinator = c;
	signature.set(c->GetComponentType<Base>(), true);
	signature.set(c->GetComponentType<Transform>(), true);
	signature.set(c->GetComponentType<LinearPlatform>(), true);
}

void LinearPlatformSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & signature) == signature) {
		platforms.Insert(entity, { coordinator, entity });
	}
	else {
		platforms.Remove(entity);
	}
}

void LinearPlatformSystem::OnEntityDestroyed(Entity entity) {
	platforms.Remove(entity);
}

void LinearPlatformSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	const float elapsed = (float)elapsed_nsec / 1000000000.0f;
	for (LinearPlatformEntity& e : platforms.GetData()) {
		Update(e, elapsed);
	}
}

void LinearPlatformSystem::Update(LinearPlatformEntity& e, float elapsed_seconds) {
	LinearPlatform* p = e.platform;
	LinearPlatform::Runtime& rt = p->rt;

	const float3 current = CurrentPosition(e.transform, e.physics);
	if (!rt.latched) {
		rt.start = current;
		rt.last_applied = current;
		rt.clock = 0.0f;
		rt.t = 0.0f;
		rt.forward = true;
		rt.latched = true;
	}
	else if (LENGHT_SQUARE_F3(SUB_F3_F3(current, rt.last_applied)) > RELATCH_EPSILON_SQ) {
		//Moved from outside: this is now the start of the path, and the platform walks it
		//again from here. See the same note in PlatformSystem::Update.
		rt.start = current;
		rt.last_applied = current;
		rt.clock = 0.0f;
		rt.t = 0.0f;
		rt.forward = true;
	}

	const float length = LENGHT_F3(p->travel);
	if (length <= 0.0f || p->speed <= 0.0f) {
		return;
	}

	rt.clock += elapsed_seconds;
	const float motion_time = rt.clock - p->delay;
	if (motion_time < 0.0f) {
		return;
	}
	const float step_seconds = (std::min)(elapsed_seconds, motion_time);

	//The step is taken in units of `t` - the fraction of the path - so a step longer
	//than what is left of the path cannot carry the platform past its own end, however
	//long a tick happens to be.
	const float step = (p->speed * step_seconds) / length;
	if (rt.forward) {
		rt.t += step;
		if (rt.t >= 1.0f) {
			rt.t = 1.0f;
			//Without ping_pong the platform stops here: `forward` stays true and `t` is
			//pinned at the end, so every later tick computes the same pose.
			if (p->ping_pong) {
				rt.forward = false;
			}
		}
	}
	else {
		rt.t -= step;
		if (rt.t <= 0.0f) {
			rt.t = 0.0f;
			rt.forward = true;
		}
	}

	const float3 target = ADD_F3_F3(rt.start, MULT_F3_F(p->travel, rt.t));
	const float3 delta = SUB_F3_F3(target, rt.last_applied);
	if (LENGHT_SQUARE_F3(delta) <= 0.0f) {
		return;
	}
	EnsureMovable(e.base, e.physics);
	ApplyPose(e.transform, e.physics, target, nullptr);
	CarryRiders(coordinator, e.base, e.physics, delta, target, float3{}, 0.0f);
	rt.last_applied = target;
}
