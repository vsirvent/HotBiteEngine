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

//Physics.h first, and it has to stay first: it includes reactphysics3d, whose headers
//call std::min/std::max, and anything that pulls in <Windows.h> ahead of it defines
//those as macros and turns every one of those calls into a syntax error. RenderSystem.cpp
//opens with the same include for the same reason.
#include <Components/Physics.h>
#include "SplatCloudSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace DirectX;

void SplatCloudSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<Base>(), true);
	signature.set(coordinator->GetComponentType<SplatCloud>(), true);

	//StaticMeshSystem's own signature, minus the Base/Transform this one already
	//requires: an entity matching it is one that system maintains.
	mesh_signature.set(coordinator->GetComponentType<Mesh>(), true);
	mesh_signature.set(coordinator->GetComponentType<Bounds>(), true);

	physics_signature.set(coordinator->GetComponentType<Physics>(), true);
}

void SplatCloudSystem::OnEntityDestroyed(ECS::Entity entity) {
	splat_clouds.Remove(entity);
}

void SplatCloudSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {
	//A world matrix has exactly one owner. A cloud that also carries mesh geometry
	//belongs to StaticMeshSystem (which composes the same matrix and additionally
	//handles bone attachment), and one carrying a rigid body belongs to PhysicsSystem,
	//which writes the body's pose straight into the Transform. Taking either here
	//would mean two threads composing the same matrix from different inputs.
	const bool owned_elsewhere =
		(entity_signature & mesh_signature) == mesh_signature ||
		(entity_signature & physics_signature) == physics_signature;

	if ((entity_signature & signature) == signature && !owned_elsewhere) {
		splat_clouds.Insert(entity, SplatCloudEntity{ coordinator, entity });
	}
	else {
		splat_clouds.Remove(entity);
	}
}

void SplatCloudSystem::Update(SplatCloudEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	Transform* transform = entity.transform;
	Base* base = entity.base;

	float3 parent_position = {};
	float4 parent_rotation = {};
	//A parent id can outlive the parent's components: destroying a composed object
	//takes the root and its parts one at a time, and this runs on another thread in
	//between. Same guard, and same reason, as StaticMeshSystem.
	const bool has_parent = base->parent != ECS::INVALID_ENTITY_ID &&
		coordinator->ContainsComponent<Components::Transform>(base->parent);
	if (has_parent) {
		Components::Transform& t = coordinator->GetComponent<Components::Transform>(base->parent);
		parent_position = t.position;
		parent_rotation = t.rotation;
	}

	//Nothing here is measured from an asset, so unlike StaticMeshSystem there is no
	//box that can change under a clean transform: dirty and the parent's pose are the
	//whole of it.
	if (!transform->dirty &&
		!(has_parent && (transform->last_parent_position != parent_position ||
			transform->last_parent_rotation != parent_rotation))) {
		return;
	}

	matrix trans = XMMatrixTranslation(transform->position.x, transform->position.y, transform->position.z);
	matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform->rotation));
	matrix scle = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z);
	transform->world_xmmatrix = scle * rot * trans;

	if (has_parent) {
		Components::Transform& pt = coordinator->GetComponent<Transform>(base->parent);
		//The parent's rotation and position, and deliberately not its scale - FBXLoader
		//gives an imported child a global transform *and* a parent, so a parent scale
		//applied here would be counted twice. The bone-attached path is StaticMeshSystem's
		//alone: the joint pose it composes lives on the parent's Mesh, and an entity with
		//a Mesh is not one this system owns.
		if (base->parent_rotation) {
			matrix r = XMMatrixRotationQuaternion({ pt.rotation.x, pt.rotation.y, pt.rotation.z, pt.rotation.w });
			transform->world_xmmatrix = transform->world_xmmatrix * r;
		}
		if (base->parent_position) {
			matrix t = XMMatrixTranslation(pt.position.x, pt.position.y, pt.position.z);
			transform->world_xmmatrix = transform->world_xmmatrix * t;
		}
	}

	//Transposed on the way out: that is the convention every shader reads `world`
	//under, SplatPreprocessCS included, which multiplies a row vector by it.
	XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(transform->world_xmmatrix));
	XMStoreFloat4x4(&transform->world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform->world_xmmatrix)));

	//prev_world_matrix is deliberately not touched - RenderSystem::LatchPreviousFrame
	//owns it, once per rendered frame. Latching it next to the matrix it is meant to
	//lag behind is what made every entity report zero motion.
	coordinator->SendEvent(this, base->id, Transform::EVENT_ID_TRANSFORM_CHANGED);
	transform->last_parent_position = parent_position;
	transform->last_parent_rotation = parent_rotation;
	transform->dirty = false;
}

void SplatCloudSystem::Update(ECS::Entity entity, int64_t elapsed_nsec, int64_t total_nsec) {
	SplatCloudEntity* se = splat_clouds.Get(entity);
	if (se) {
		Update(*se, elapsed_nsec, total_nsec);
	}
}

void SplatCloudSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = splat_clouds.GetData().begin(); it != splat_clouds.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}

