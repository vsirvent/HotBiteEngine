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

#include <Components\Physics.h>
#include "StaticMeshSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace DirectX;

void StaticMeshSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<Bounds>(), true);
	signature.set(coordinator->GetComponentType<Mesh>(), true);
	signature.set(coordinator->GetComponentType<Base>(), true);
}


void StaticMeshSystem::OnEntityDestroyed(ECS::Entity entity) {
	static_meshes.Remove(entity);
}

void StaticMeshSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {

	//Entities with physics component are managed by the physics component, StaticMeshSystem is for meshes with no physics
	if ((entity_signature & signature) == signature)
	{
		StaticMeshEntity mesh{ coordinator, entity };
		static_meshes.Insert(entity, mesh);
		Init(mesh);

	}
	else
	{
		static_meshes.Remove(entity);
	}
}

//XMFLOAT3 has no comparison of its own, and this is only ever asking "is this the
//same box I measured last time".
static bool SameBox(const box& a, const box& b) {
	return a.Center.x == b.Center.x && a.Center.y == b.Center.y && a.Center.z == b.Center.z &&
		a.Extents.x == b.Extents.x && a.Extents.y == b.Extents.y && a.Extents.z == b.Extents.z;
}

//The socket matrix of a bone-attached entity: where the joint named by
//Base::parent_bone sits, in the parent mesh's own space, for the pose the parent is
//playing right now. False when this entity rides no bone, or when the bone cannot be
//resolved - in which case the caller falls back to the plain parent composition rather
//than to identity, which would drop the attachment onto the parent's origin.
//
//The index is cached in the Base because resolving it is a string search over every
//joint; it is invalidated by whoever changes the bone or the parent (Base::FromJson,
//World::SpawnInstance).
static bool GetParentJoint(ECS::Coordinator* coordinator, Base* base, matrix& out) {
	if (base->parent == ECS::INVALID_ENTITY_ID || base->parent_bone.empty() ||
		!coordinator->ContainsComponent<Transform>(base->parent) ||
		!coordinator->ContainsComponent<Base>(base->parent) ||
		!coordinator->ContainsComponent<Mesh>(base->parent)) {
		return false;
	}
	Mesh& parent_mesh = coordinator->GetComponent<Mesh>(base->parent);
	if (base->parent_joint == Base::UNRESOLVED_JOINT) {
		//Turning tracking on is what makes the parent keep its joint poses at all, so
		//it has to happen before the first read - and only for a mesh something is
		//actually attached to.
		parent_mesh.TrackJoints();
		base->parent_joint = parent_mesh.FindJoint(base->parent_bone);
		if (base->parent_joint < 0) {
			printf("StaticMeshSystem: entity %s rides unknown bone '%s' of %s.\n",
				base->name.c_str(), base->parent_bone.c_str(),
				coordinator->GetComponent<Base>(base->parent).name.c_str());
		}
	}
	return parent_mesh.GetJointPose(base->parent_joint, out);
}

void StaticMeshSystem::Init(StaticMeshEntity& entity) {

	Transform* transform = entity.transform;
	Bounds* bounds = entity.bounds;
	Mesh* mesh = entity.mesh;

	//Init default values
	entity.transform->dirty = true;
	Update(entity, 0, 0);
}

void StaticMeshSystem::Update(StaticMeshEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	Transform* transform = entity.transform;
	Bounds* bounds = entity.bounds;
	Base* base = entity.base;
	Mesh* mesh = entity.mesh;

	//We only update view if invalid
	float3 parent_position = {};
	float4 parent_rotation = {};
	//A parent that no longer exists is not an error to assert on: deleting a composed
	//object destroys the root and its parts one by one, and this runs on another thread
	//in between. The child simply stands where it is until it is destroyed too - reading
	//the Transform of a destroyed entity is what used to take the process down.
	const bool has_parent = entity.base->parent != ECS::INVALID_ENTITY_ID &&
		coordinator->ContainsComponent<Components::Transform>(entity.base->parent);
	if (has_parent) {
		Components::Transform& t = coordinator->GetComponent<Components::Transform>(entity.base->parent);
		parent_position = t.position;
		parent_rotation = t.rotation;

	}
	//A socketed entity has to be rebuilt every frame: what moves it is the parent's
	//animation, and no flag anywhere says a pose changed.
	matrix joint_pose{};
	const bool bone_attached = GetParentJoint(coordinator, base, joint_pose);

	//The mesh's own box, which for a skinned mesh is the box of the animation it plays
	//rather than of the bind pose its vertices are stored in - see Mesh::GetLocalBox.
	box measured{};
	const bool has_box = mesh->GetLocalBox(measured);
	//That box belongs to the animation, not to the transform: it changes when the clip
	//changes, which is not something transform->dirty ever says. Comparing it against
	//what the Bounds already holds is cheaper than tracking the clip.
	const bool box_changed = has_box && !SameBox(measured, bounds->local_box);

	if ((entity.transform->dirty || box_changed || bone_attached ||
		(has_parent &&
		(entity.transform->last_parent_position != parent_position || entity.transform->last_parent_rotation != parent_rotation)))) {
		if (has_box) {
			bounds->local_box = measured;
		}
		bounds->final_box = bounds->local_box;

		matrix trans = XMMatrixTranslation(transform->position.x, transform->position.y, transform->position.z);
		matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform->rotation));
		matrix scle = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z);
		transform->world_xmmatrix = scle * rot * trans;

		vector4d extents = XMLoadFloat3(&bounds->final_box.Extents);
		extents = XMVector4Transform(extents, transform->world_xmmatrix);
		if (has_parent) {
			Components::Transform& pt = coordinator->GetComponent<Transform>(entity.base->parent);
			if (bone_attached) {
				//Riding a joint: the local transform is an offset from the joint, the
				//joint sits in the parent mesh's own space, and that space is placed in
				//the world by the parent's whole world matrix - scale included, which is
				//the one parenting path that carries it. parent_position/parent_rotation
				//say nothing here; they describe the position-only composition below.
				transform->world_xmmatrix = transform->world_xmmatrix * joint_pose * pt.world_xmmatrix;
			}
			else {
				if (base->parent_rotation) {
					matrix r = XMMatrixRotationQuaternion({ pt.rotation.x, pt.rotation.y, pt.rotation.z, pt.rotation.w });
					transform->world_xmmatrix = transform->world_xmmatrix * r;
				}
				if (base->parent_position) {
					matrix t = XMMatrixTranslation(pt.position.x, pt.position.y, pt.position.z);
					transform->world_xmmatrix = transform->world_xmmatrix * t;
				}
			}
		}
		XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(transform->world_xmmatrix));
		XMStoreFloat4x4(&transform->world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform->world_xmmatrix)));

		transform->prev_world_matrix = transform->world_matrix;

		bounds->local_box.Transform(bounds->final_box, transform->world_xmmatrix);

		BoundingOrientedBox local_oriented;
		local_oriented.Center = bounds->local_box.Center;
		local_oriented.Extents = bounds->local_box.Extents;
		local_oriented.Transform(bounds->bounding_box, transform->world_xmmatrix);

		coordinator->SendEvent(this, base->id, Transform::EVENT_ID_TRANSFORM_CHANGED);
		entity.transform->last_parent_position = parent_position;
		entity.transform->last_parent_rotation = parent_rotation;
		transform->dirty = false;
	}
}

void StaticMeshSystem::Update(ECS::Entity entity, int64_t elapsed_nsec, int64_t total_nsec) {
	StaticMeshEntity* se = static_meshes.Get(entity);
	if (se) {
		Update(*se, elapsed_nsec, total_nsec);
	}
}

void StaticMeshSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = static_meshes.GetData().begin(); it != static_meshes.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}

