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
		//World::LoadSky wires this for a sky read from a level; a sky built any other way
		//(a game assembling it in code, the editor's Add Component) has no other place to
		//get it, and the renderer dereferences it every frame.
		SkyEntity& added = *skies.Get(entity);
		added.sky->dir_light = added.dl;
		//Bring the sun, the sky colour and the ambient cycle to this time of day now, so the
		//first frame is lit right instead of by whatever the light was built with.
		Update(added, 0, 0);
	}
	else
	{
		skies.Remove(entity);
	}	
}

void SkySystem::Update(SkyEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	Sky& sky = *entity.sky;
	sky.dir_light = entity.dl;
	float elapsed_sec = (float)(elapsed_nsec * sky.second_speed) / (float)(NSEC);
	sky.second_of_day += elapsed_sec;

	//The sun only moves when the clock has moved far enough to be seen. It used to be a
	//whole minute, which at a game's fast-forward speeds is a visible step and at 1x is
	//nothing; a few seconds keeps it smooth without dirtying the light every frame.
	//fabs also catches the clock being set from outside, wrapping included.
	if (fabsf(sky.second_of_day - sky.applied_second_of_day) < SUN_UPDATE_SECONDS) {
		return;
	}
	sky.applied_second_of_day = sky.second_of_day;
	sky.current_minute = (int)(sky.second_of_day / 60.0f);

	float t = 2.0f * XM_PI * sky.second_of_day / 86400.0f;
	float x = sin(t);
	float y = -cos(t);
	float z = y / 2.0f;
	entity.dl->data.direction = { x, y, z };
	entity.dl->data.intensity = min(max(y + 0.5f, 0.1f), 1.0f);
	entity.dl->dirty = true;

	float wfull = min(max(y*5.0f, 0.0f), 1.0f);
	float wmid = max(1.0f - abs(y*5.0f), 0.0f);
	sky.current_backcolor = { sky.mid_backcolor.x * wmid + sky.day_backcolor.x * wfull,
							  sky.mid_backcolor.y * wmid + sky.day_backcolor.y * wfull,
							  sky.mid_backcolor.z * wmid + sky.day_backcolor.z * wfull };

	if (sky.ambient_cycle) {
		//0 below the horizon band, 1 above it, linear across the ~0.2 of sun height either
		//side of the horizon so dusk and dawn fade instead of switching.
		float k = min(max(y * 2.5f + 0.5f, 0.0f), 1.0f);
		auto mix = [k](const float3& night, const float3& day) {
			return float3{ night.x + (day.x - night.x) * k,
						   night.y + (day.y - night.y) * k,
						   night.z + (day.z - night.z) * k };
		};
		entity.al->GetData().colorUp = mix(sky.ambient_night_up, sky.ambient_day_up);
		entity.al->GetData().colorDown = mix(sky.ambient_night_down, sky.ambient_day_down);
	}

	vector4d xm_rot = XMQuaternionRotationAxis({ 1.0f, 0.5f, 0.0f }, t);
	XMStoreFloat4(&entity.transform->rotation, xm_rot);
	entity.transform->dirty = true;
}

void SkySystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = skies.GetData().begin(); it != skies.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}