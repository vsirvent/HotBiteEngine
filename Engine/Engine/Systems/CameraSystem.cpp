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

#include <Components\camera.h>
#include <Core\DXCore.h>
#include "CameraSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

void CameraSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Base>(), true);
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<Camera>(), true);	
}

void CameraSystem::OnEntityDestroyed(Entity entity) {
	cameras.Remove(entity);
}

void CameraSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & signature) == signature)
	{
		CameraData cam{ coordinator, entity };
		cameras.Insert(entity, cam);
		Init(cam);
	}
	else
	{
		cameras.Remove(entity);
	}
}

bool CameraSystem::Init(CameraData& entity) {
	entity.transform->dirty = true;
	float aspect_ratio = (float)DXCore::Get()->GetWidth() / (float)DXCore::Get()->GetHeight();
	entity.camera->xm_projection = XMMatrixPerspectiveFovLH(
		0.25f * XM_PI,		// Field of View Angle
		aspect_ratio,		// Aspect ratio
		1.0f,						// Near clip plane distance
		10000.0f);					// Far clip plane distance
	XMStoreFloat4x4(&entity.camera->projection, XMMatrixTranspose(entity.camera->xm_projection));
	XMStoreFloat4x4(&entity.camera->inverse_projection, XMMatrixTranspose(XMMatrixInverse(nullptr, entity.camera->xm_projection)));
	Update(entity, 0, 0);
	return true;
}

CameraSystem::~CameraSystem() {
}

void CameraSystem::SetPosition(CameraData& entity, float3 pos)
{
	entity.transform->position = pos;
	entity.transform->dirty = true;
}

void CameraSystem::SetRotation(CameraData& entity, float3 rot)
{
	entity.camera->rotation = rot;
	entity.transform->dirty = true;
}

void CameraSystem::RotateX(CameraData& entity, float x)
{
	entity.camera->rotation.x += x;
	if (entity.camera->rotation.x > XM_PI) entity.camera->rotation.x = -XM_PI;
	if (entity.camera->rotation.x < -XM_PI) entity.camera->rotation.x = XM_PI;
	entity.transform->dirty = true;
}

void CameraSystem::RotateY(CameraData& entity, float y)
{
	entity.camera->rotation.y += y;
	if (entity.camera->rotation.y > XM_PI) entity.camera->rotation.y = -XM_PI;
	if (entity.camera->rotation.y < -XM_PI) entity.camera->rotation.y = XM_PI;
	entity.transform->dirty = true;
}

void CameraSystem::RotateZ(CameraData& entity, float z)
{
	entity.camera->rotation.z += z;
	if (entity.camera->rotation.z > XM_PI) entity.camera->rotation.z = -XM_PI;
	if (entity.camera->rotation.z < -XM_PI) entity.camera->rotation.z = XM_PI;
	entity.transform->dirty = true;
}

void CameraSystem::Zoom(CameraSystem::CameraData& entity, float delta) {
	float3 dir = entity.camera->direction - entity.transform->position;
	vector3d xm_dir = XMVector3Normalize(XMLoadFloat3(&dir)) * (-delta);
	entity.transform->position.x += xm_dir.m128_f32[0];
	entity.transform->position.y += xm_dir.m128_f32[1];
	entity.transform->position.z += xm_dir.m128_f32[2];
	entity.transform->dirty = true;
}

void CameraSystem::Update(CameraData& entity, int64_t elapsed_nsec, int64_t total_nsec)
{
	bool updated = false;
	float3 parent_pos = {};
	if (entity.base->parent != ECS::INVALID_ENTITY_ID) {
		parent_pos = entity.camera->last_parent_pos;
		Components::Transform& parent_transform = coordinator->GetComponent<Transform>(entity.base->parent);
		if (parent_transform.position != entity.camera->last_parent_pos) {
			entity.transform->dirty = true;
			entity.camera->last_parent_pos = parent_transform.position;
			parent_pos = parent_transform.position;
		}
	}
	if (entity.transform->dirty == true) {
		vector3d pos = XMVectorSet(entity.transform->position.x, entity.transform->position.y, entity.transform->position.z, 1.0f);
		entity.camera->xm_rotation = XMQuaternionRotationRollPitchYaw(entity.camera->rotation.x, entity.camera->rotation.y, entity.camera->rotation.z);

		if (entity.base->parent != ECS::INVALID_ENTITY_ID) {
			float4 ppos{ parent_pos.x + entity.camera->direction.x, parent_pos.y + entity.camera->direction.y, parent_pos.z + entity.camera->direction.z, 1.0f };
			vector3d vparent_pos = XMVectorSet(ppos.x, ppos.y, ppos.z, ppos.w);
			XMMATRIX parent_trans = XMMatrixTranslation(ppos.x, ppos.y, ppos.z);
			pos = XMVector3Rotate(pos, entity.camera->xm_rotation);
			pos = XMVector3Transform(pos, parent_trans);
			entity.camera->xm_direction = XMVector3Normalize(XMVectorSubtract(vparent_pos, pos));
			//XMStoreFloat3(&entity.camera->direction, entity.camera->xm_direction);
		}
		else {
			vector3d dir_pos = XMVectorSet(entity.camera->direction.x, entity.camera->direction.y, entity.camera->direction.z, 1.0f);
			//Translate to dir point, rotate and move back to original position
			matrix mdir = XMMatrixTranslation(entity.camera->direction.x, entity.camera->direction.y, entity.camera->direction.z);			
			matrix mrot = XMMatrixRotationRollPitchYaw(entity.camera->rotation.x, entity.camera->rotation.y, entity.camera->rotation.z);
			matrix dirt = XMMatrixInverse(nullptr, mdir) * mrot * mdir;
			pos = XMVector3Transform(pos, dirt);
			entity.camera->xm_direction = XMVector3Normalize(XMVectorSubtract(dir_pos, pos));
		}

		XMStoreFloat3(&entity.camera->world_position, pos);
		vector4d up = XMVectorSet(0, 1, 0, 0);
		entity.camera->xm_view = XMMatrixLookToLH(
			pos,     // The position of the "camera"
			entity.camera->xm_direction,     // Direction the camera is looking
			up);     // "Up" direction in 3D space (prevents roll)
		XMStoreFloat3(&entity.camera->final_position, pos);
		
		XMStoreFloat4x4(&entity.camera->view, XMMatrixTranspose(entity.camera->xm_view));
		XMStoreFloat4x4(&entity.camera->inverse_view, XMMatrixTranspose(XMMatrixInverse(nullptr, entity.camera->xm_view)));
		
		entity.camera->xm_view_projection = entity.camera->xm_view * entity.camera->xm_projection;
		XMStoreFloat4x4(&entity.camera->inverse_view_projection, XMMatrixTranspose(XMMatrixInverse(nullptr, entity.camera->xm_view_projection)));
		updated = true;
		entity.transform->dirty = false;
		Event ev(this, EVENT_ID_CAMERA_MOVED);
		ev.SetParam<CameraData*>(EVENT_PARAM_CAMERA_DATA, &entity);
		coordinator->SendEvent(ev);
	}
}

void CameraSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = cameras.GetData().begin(); it != cameras.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}	
}

ECS::EntityVector<CameraSystem::CameraData>& CameraSystem::GetCameras() {
	return cameras;
}
