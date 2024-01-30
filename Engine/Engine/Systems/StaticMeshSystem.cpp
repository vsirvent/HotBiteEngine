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

void StaticMeshSystem::Init(StaticMeshEntity& entity) {

	Transform* transform = entity.transform;
	Bounds* bounds = entity.bounds;
	Mesh* mesh = entity.mesh;

	//Init default values
	XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(XMMatrixIdentity()));		
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
	if (entity.base->parent != ECS::INVALID_ENTITY_ID) {
		Components::Transform& t = coordinator->GetComponent<Components::Transform>(entity.base->parent);
		parent_position = t.position;
	}

	if ((entity.transform->dirty ||
		(entity.base->parent != ECS::INVALID_ENTITY_ID && entity.transform->last_parent_position != parent_position))) {
		auto minV = mesh->GetData()->minDimensions;
		auto maxV = mesh->GetData()->maxDimensions;
		
		float3 center = float3((maxV.x + minV.x) / 2.0f, (maxV.y + minV.y) / 2.0f, (maxV.z + minV.z) / 2.0f);
		float3 extends = float3(abs(maxV.x - minV.x) / 2.0f, abs(maxV.y - minV.y) / 2.0f, abs(maxV.z - minV.z) / 2.0f);
		bounds->bounding_box.Extents = extends;
		bounds->bounding_box.Center = center;
		bounds->local_box = bounds->bounding_box;
		bounds->final_box = bounds->bounding_box;

		matrix trans = XMMatrixTranslation(transform->position.x, transform->position.y, transform->position.z);
		matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform->rotation));
		matrix scle = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z);
		transform->world_xmmatrix = scle * rot * trans;

		vector4d extents = XMLoadFloat3(&bounds->final_box.Extents);
		extents = XMVector4Transform(extents, transform->world_xmmatrix);
		if (entity.base->parent >= 0) {
			Components::Transform& pt = coordinator->GetComponent<Transform>(entity.base->parent);
			if (base->parent_rotation) {
				matrix r = XMMatrixRotationQuaternion({ pt.rotation.x, pt.rotation.y, pt.rotation.z, pt.rotation.w });
				transform->world_xmmatrix = transform->world_xmmatrix * r;
			}
			if (base->parent_position) {
				matrix t = XMMatrixTranslation(pt.position.x, pt.position.y, pt.position.z);
				transform->world_xmmatrix = transform->world_xmmatrix * t;
			}
		}
		XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(transform->world_xmmatrix));
		XMStoreFloat4x4(&transform->world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform->world_xmmatrix)));

		float4 e4;
		XMStoreFloat4(&e4, extents);
		bounds->final_box.Extents.x = abs(e4.x / e4.w);
		bounds->final_box.Extents.y = abs(e4.y / e4.w);
		bounds->final_box.Extents.z = abs(e4.z / e4.w);
		vector3d pos = XMVector3TransformCoord(XMLoadFloat3(&bounds->bounding_box.Center), transform->world_xmmatrix);
		XMStoreFloat3(&bounds->final_box.Center, pos);		
		coordinator->SendEvent(this, base->id, Transform::EVENT_ID_TRANSFORM_CHANGED);
		entity.transform->last_parent_position = parent_position;
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

