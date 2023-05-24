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

#pragma once

#include <ECS/System.h>
#include <ECS/EntityVector.h>
#include <Components/Physics.h>
#include <World.h>
#include <Systems/CameraSystem.h>
#include "GameComponents.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;


class GameCameraSystem : public ECS::System, public EventListener {
private:

	//Here we store a vector with the game players and another with the fire entities.
	ECS::Signature camera_signature;
	ECS::EntityVector<Systems::CameraSystem::CameraData> cameras;
	ECS::Signature player_signature;
	ECS::EntityVector<GamePlayerData> players;
	ECS::Signature terrain_signature;
	ECS::EntityVector<TerrainData> terrains;
	bool game_over = false;
	//Here we store the coordinator of the game.
	ECS::Coordinator* coordinator = nullptr;

	//This is the engine camera system that includes useful methods to be
	//used in our game system
	std::shared_ptr<Systems::CameraSystem> camera_system;
	static constexpr float MIN_ZOOM = 2.0f;
	static constexpr float MAX_ZOOM = 30.0f;
	static constexpr float MIN_CAMERA_TO_TERRAIN_DISTANCE = 3.0f;

	float current_zoom = 15.0f;
	bool is_zooming = false;
	bool is_checking_camera = false;
	
public:

	//Called by coordinator when the system is registered in it.
	void OnRegister(ECS::Coordinator* c) override {
		this->coordinator = c;
		EventListener::Init(c);
		camera_system = c->GetSystem<Systems::CameraSystem>();
		camera_signature = Systems::CameraSystem::CameraData::GetSignature(c);
		player_signature = GamePlayerData::GetSignature(c);
		terrain_signature = TerrainData::GetSignature(c);
		AddEventListener(DXCore::EVENT_ID_MOUSE_WHEEL, std::bind(&GameCameraSystem::OnMouseWheel, this, std::placeholders::_1));
		AddEventListener(DXCore::EVENT_ID_MOUSE_MOVE, std::bind(&GameCameraSystem::OnMouseMove, this, std::placeholders::_1));
		AddEventListener(Systems::CameraSystem::EVENT_ID_CAMERA_MOVED, std::bind(&GameCameraSystem::CheckCameraPosition, this, std::placeholders::_1));
	}

	//Called by coordinator every time an entity is destroyed.
	void OnEntityDestroyed(Entity entity) override {
		//Remove can be called always, it only perform action when the entity is in the vector.
		cameras.Remove(entity);
		players.Remove(entity);
		terrains.Remove(entity);
	}

	void SetGameOver(bool active) {
		game_over = active;
	}

	//Called by coordinator every time the entity signature is created or changed.
	//This is, a component has been added or removed to the entity and NotifySignatureChange is called.
	//NotifySignatureChange is not called internally automatically to avoid spamming when we add/remove several components.
	//Instead, we call it manually when we have finished adding/removing components.
	void OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) override {
		if ((entity_signature & camera_signature) == camera_signature)
		{
			cameras.Insert(entity, Systems::CameraSystem::CameraData{ coordinator, entity });
		}
		else {
			cameras.Remove(entity);
		}
		if ((entity_signature & terrain_signature) == terrain_signature)
		{
			terrains.Insert(entity, TerrainData{ coordinator, entity });
		}
		else {
			terrains.Remove(entity);
		}
		if ((entity_signature & player_signature) == player_signature)
		{
			players.Insert(entity, GamePlayerData{ coordinator, entity });
		}
		else {
			players.Remove(entity);
		}
	}

	//Event DXCore::EVENT_ID_MOUSE_WHEEL received when the mouse wheel is moved, we subscribed to the event in the constructor.
	void OnMouseWheel(ECS::Event& ev) {
		if (game_over) {
			return;
		}
		
		//Use mouse wheel for camera zoom
		float amount = ev.GetParam<float>(DXCore::PARAM_ID_WHEEL) * -5.0f;
		float new_zoom = current_zoom + amount;
		if (new_zoom > MIN_ZOOM && new_zoom < MAX_ZOOM && !is_zooming) {
			//Camera zoom is smooth by executing it in a timer
			is_zooming = true;
			Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RegisterTimer(1000000000 / 60,
				[this, new_zoom, &camera = cameras.GetData()[0]](const Scheduler::TimerData& t) {
					bool ret = true;
					float delta = std::clamp((new_zoom - current_zoom) / 5.0f, -5.0f, 5.0f);
					if (abs(current_zoom - new_zoom) > 0.1f) {
						camera_system->Zoom(camera, delta);
						current_zoom += delta;
					}
					else {
						is_zooming = false;
					}
					return is_zooming;
				});
		}
	}

	//Event DXCore::EVENT_ID_MOUSE_MOVE received when the mouse wheel is moved, we subscribed to the event in the constructor.
	void OnMouseMove(ECS::Event& ev) {
		if (game_over) {
			return;
		}
		//Rotate only first camera of the registered cameras if right button pressed
		if (ev.GetParam<int>(DXCore::PARAM_ID_BUTTON) == MK_RBUTTON) {
			auto& camera = cameras.GetData()[0];
			int x = ev.GetParam<int>(DXCore::PARAM_RELATIVE_ID_X);
			int y = ev.GetParam<int>(DXCore::PARAM_RELATIVE_ID_Y);
			camera_system->RotateY(camera, (float)x * 0.002f);
			bool can_rotate_x = true;
			if (y < 0.0f) {
				for (const auto& t : terrains.GetConstData()) {
					float distance_to_terrain = 0.0f;
					reactphysics3d::Vector3 r0 = { camera.camera->final_position.x, camera.camera->final_position.y + 100.0f, camera.camera->final_position.z };
					reactphysics3d::Vector3 r1 = { r0.x, camera.camera->final_position.y - 100.0f, r0.z };
					reactphysics3d::RaycastInfo info;
					if (t.physics->body->raycast({ r0, r1 }, info)) {
						float distance = camera.camera->final_position.y - info.worldPoint.y;
						if (distance < MIN_CAMERA_TO_TERRAIN_DISTANCE) {
							can_rotate_x = false;
							break;
						}
					}
				}
			}
			if (can_rotate_x) {
				camera_system->RotateX(camera, (float)y * 0.002f);
			}
		}
	}

	//Event CameraSystem::EVENT_ID_CAMERA_MOVED received every time the camera is moved,
	//we check if the camera is clipping with the terrain and if so we correct the position
	void CheckCameraPosition(ECS::Event& ev) {
		if (!is_checking_camera) {
			this->is_checking_camera = true;
			//Smooth correct position to avoid camera clipping with terrain
			Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / 60,
				[this](const Scheduler::TimerData& t) {
					physics_mutex.lock();
					bool ret = false;
					for (auto& camera : cameras.GetData()) {
						for (const auto& t : terrains.GetConstData()) {
							reactphysics3d::Vector3 r0 = { camera.camera->final_position.x, camera.camera->final_position.y + 100.0f, camera.camera->final_position.z };
							reactphysics3d::Vector3 r1 = { r0.x, camera.camera->final_position.y - 100.0f, r0.z };
							reactphysics3d::RaycastInfo info;
							if (t.physics->body->raycast({ r0, r1 }, info)) {
								float distance = camera.camera->final_position.y - info.worldPoint.y;
								if (distance < MIN_CAMERA_TO_TERRAIN_DISTANCE) {
									float diff = abs(MIN_CAMERA_TO_TERRAIN_DISTANCE - distance);
									camera_system->RotateX(camera, diff / 100.0f);
									ret = true;
									break;
								}
							}
						}
					}
					physics_mutex.unlock();

					this->is_checking_camera = ret;
					return ret;
				});
		}
	}
};