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
#include "DirectionalLightSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

void DirectionalLightSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	dirlight_signature.set(coordinator->GetComponentType<Base>(), true);
	dirlight_signature.set(coordinator->GetComponentType<DirectionalLight>(), true);

	camera_signature.set(coordinator->GetComponentType<Base>(), true);
	camera_signature.set(coordinator->GetComponentType<Transform>(), true);
	camera_signature.set(coordinator->GetComponentType<Camera>(), true);
}


void DirectionalLightSystem::OnEntityDestroyed(Entity entity) {
	lights.Remove(entity);
	cameras.Remove(entity);
}

void DirectionalLightSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & dirlight_signature) == dirlight_signature)
	{
		lights.Insert(entity, DirectionalLightEntity{ coordinator, entity });
	}
	else
	{
		lights.Remove(entity);
	}
	if ((entity_signature & camera_signature) == camera_signature)
	{
		cameras.Insert(entity, CameraSystem::CameraData{ coordinator, entity });
	}
	else
	{
		cameras.Remove(entity);
	}
}

void DirectionalLightSystem::Update(DirectionalLightEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	if (!cameras.GetData().empty()) {
		CameraSystem::CameraData& camera = cameras.GetData()[0];
		assert(!cameras.GetData().empty() && "No cameras detected");
		if ((entity.light->dirty || entity.light->last_cam_pos != camera.camera->world_position) && entity.light->CastShadow()) {

			matrix lightProjection, spotView, toShadow;
			float3 cam_pos = { camera.camera->world_position.x, 0.0f, camera.camera->world_position.z };

			entity.light->last_cam_pos = camera.camera->world_position;
			float w = (float)entity.light->texture.Width();
			float perspective_w = w / 50.0f;

			vector3d XMLightPos = XMLoadFloat3(&entity.light->data.direction) * 500.0f;
			vector3d XMCamPosition = XMLoadFloat3(&cam_pos);
			vector3d XMDir = (camera.camera->xm_direction) * perspective_w * 0.2f;

			XMDir = XMVectorSetY(XMDir, 0.0f);
			XMLightPos = XMCamPosition + XMLightPos + XMDir;


			lightProjection = XMMatrixOrthographicLH(perspective_w, perspective_w, 1.0f, 1000.0f);
			XMStoreFloat4x4(&entity.light->lightPerspectiveValues, lightProjection);
			if (LENGHT_SQUARE_F3(entity.light->data.direction) != 0.0f) {
				vector4d dir = XMVectorSet(-entity.light->data.direction.x, -entity.light->data.direction.y, -entity.light->data.direction.z, 0.0f);
				vector4d up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
				XMVECTOR right = XMVector3Cross(up, dir);
				if (XMVector3Length(right).m128_f32[0] < 0.0001f) {
					right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
				}
				else {
					right = XMVector3Normalize(right);
				}
				up = XMVector3Cross(dir, right);
				spotView = XMMatrixLookToLH(
					XMLightPos,     // The position of the "camera"
					dir,     // Direction the camera is looking
					up);     // "Up" direction in 3D space (prevents roll)

				toShadow = spotView * lightProjection;
				XMStoreFloat4x4(&entity.light->viewMatrix, XMMatrixTranspose(toShadow));
				XMStoreFloat4x4(&entity.light->projectionMatrix, lightProjection);
			}
			entity.light->dirty = false;
		}
	}
}

void DirectionalLightSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = lights.GetData().begin(); it != lights.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}