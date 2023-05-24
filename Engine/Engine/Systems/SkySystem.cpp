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
#include "SkySystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

void SkySystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	sky_signature.set(coordinator->GetComponentType<Base>(), true);
	sky_signature.set(coordinator->GetComponentType<Transform>(), true);
	sky_signature.set(coordinator->GetComponentType<DirectionalLight>(), true);
	sky_signature.set(coordinator->GetComponentType<AmbientLight>(), true);
	sky_signature.set(coordinator->GetComponentType<Material>(), true);
	sky_signature.set(coordinator->GetComponentType<Sky>(), true);
	sky_signature.set(coordinator->GetComponentType<Mesh>(), true);
}


void SkySystem::OnEntityDestroyed(Entity entity) {
	skies.Remove(entity);
}

void SkySystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & sky_signature) == sky_signature)
	{
		skies.Insert(entity, SkyEntity{ coordinator, entity });
	}
	else
	{
		skies.Remove(entity);
	}	
}

void SkySystem::Update(SkyEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	float elapsed_sec = (float)(elapsed_nsec * entity.sky->second_speed) / (float)(NSEC);
	entity.sky->second_of_day += elapsed_sec;
	int current = (int)(entity.sky->second_of_day/60.0f);
	if (current != entity.sky->current_minute) {
		float t = 2.0f * XM_PI * entity.sky->second_of_day / 86400.0f;
		float x = sin(t);
		float y = -cos(t);
		float z = y / 2.0f;
		entity.dl->data.direction = { x, y, z };
		entity.dl->data.intensity = min(max(y + 0.5f, 0.1f), 1.0f);
		entity.dl->dirty = true;
		entity.sky->current_minute = current;
		
		float wfull = min(max(y*5.0f, 0.0f), 1.0f);
		float wmid = max(1.0f - abs(y*5.0f), 0.0f);
		entity.sky->current_backcolor = { entity.sky->mid_backcolor.x * wmid + entity.sky->day_backcolor.x * wfull,
								          entity.sky->mid_backcolor.y * wmid + entity.sky->day_backcolor.y * wfull,
								          entity.sky->mid_backcolor.z * wmid + entity.sky->day_backcolor.z * wfull };

		vector4d xm_rot = XMQuaternionRotationAxis({ 1.0f, 0.5f, 0.0f }, t);
		XMStoreFloat4(&entity.transform->rotation, xm_rot);
		entity.transform->dirty = true;
		static int n = 0;
		if (n++ % 120 == 0) {
			int h = current / 3600;
			int m = (current % 3600) / 60;
			printf("Current sky time %d:%d == %f, %f, %f. Color %f, %f, %f\n", h, m, x, y, z, entity.sky->current_backcolor.x, entity.sky->current_backcolor.y, entity.sky->current_backcolor.z);
		}
	}
}

void SkySystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = skies.GetData().begin(); it != skies.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}