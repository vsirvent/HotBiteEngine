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
#include "PointLightSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

void PointLightSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Base>(), true);
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<PointLight>(), true);
}

void PointLightSystem::OnEntityDestroyed(ECS::Entity entity) {
	lights.Remove(entity);
}

void PointLightSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {
	if ((entity_signature & signature) == signature)
	{
		PointLightEntity pl{ coordinator, entity };
		lights.Insert(entity, pl);
	}
	else
	{
		lights.Remove(entity);
	}
}

void PointLightSystem::Update(PointLightEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {

	//We only update view if invalid
	float3 parent_position = {};
	if (entity.base->parent != ECS::INVALID_ENTITY_ID) {
		Components::Transform& t = coordinator->GetComponent<Components::Transform>(entity.base->parent);
		parent_position = t.position;
	}
	if ((entity.transform->dirty || entity.light->dirty || entity.transform->last_parent_position != parent_position)) {
		matrix lightProjection, positionMatrix, spotView, toShadow;
		lightProjection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0, 0.1f, entity.light->data.range);
		float3 worldPosition = entity.transform->position;
		if (entity.base->parent >= 0) {
			Components::Transform& pt = coordinator->GetComponent<Transform>(entity.base->parent);
			//matrix parent_trans = pt.world_xmmatrix;

			vector3d wpos = XMVectorSet(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f);

			matrix t = XMMatrixTranslation(pt.position.x, pt.position.y, pt.position.z);
			matrix r = XMMatrixRotationQuaternion({ pt.rotation.x, pt.rotation.y, pt.rotation.z, pt.rotation.w });
			matrix pmatrix = r * t;
			wpos = XMVector3Transform(wpos, pmatrix);
			XMStoreFloat3(&worldPosition, wpos);			
			entity.transform->last_parent_position = parent_position;
		}
		entity.light->data.position = worldPosition;
		positionMatrix = XMMatrixTranslation(-worldPosition.x, -worldPosition.y, -worldPosition.z);
		XMStoreFloat4x4(&entity.light->lightPerspectiveValues, lightProjection);

		if (entity.light->CastShadow()) {
			// Cube +X
			spotView = XMMatrixRotationY(XM_PI + XM_PIDIV2);
			toShadow = positionMatrix * spotView * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[0], XMMatrixTranspose(toShadow));

			// Cube -X
			spotView = XMMatrixRotationY(XM_PIDIV2);
			toShadow = positionMatrix * spotView * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[1], XMMatrixTranspose(toShadow));

			// Cube +Y
			spotView = XMMatrixRotationX(XM_PIDIV2);
			toShadow = positionMatrix * spotView * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[2], XMMatrixTranspose(toShadow));

			// Cube -Y
			spotView = XMMatrixRotationX(XM_PI + XM_PIDIV2);
			toShadow = positionMatrix * spotView * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[3], XMMatrixTranspose(toShadow));

			// Cube +Z
			toShadow = positionMatrix * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[4], XMMatrixTranspose(toShadow));

			// Cube -Z
			spotView = XMMatrixRotationY(XM_PI);
			toShadow = positionMatrix * spotView * lightProjection;
			XMStoreFloat4x4(&entity.light->viewMatrix[5], XMMatrixTranspose(toShadow));
			XMStoreFloat4x4(&entity.light->projectionMatrix, lightProjection);
			entity.transform->dirty = false;
			entity.light->dirty = false;
		}
	}
}

void PointLightSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = lights.GetData().begin(); it != lights.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}