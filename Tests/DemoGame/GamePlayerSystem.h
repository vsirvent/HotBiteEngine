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

#include <Core\PhysicsCommon.h>
#include <Core\DXCore.h>
#include <World.h>
#include <Windows.h>
#include <Systems\RTSCameraSystem.h>
#include <GUI\GUI.h>
#include <GUI\Label.h>
#include <GUI\TextureWidget.h>

#include "GameComponents.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::UI;


/**
 * This is an example of a game system: This one is created to manage the player:
 * Movement, animations, etc...This one is based on PlayerSystem, that provides some help functions to move/jump the player and
 * a data container for the basic player components (PlayerData).
 * You can create as many systems as you need to manage the entities in your game, just
 * register the system in the world (world.RegisterSystem<>) and you can use it.
 */
class GamePlayerSystem : public Systems::PlayerSystem, public ECS::EventListener {
public:
	static inline HotBite::Engine::ECS::EventId EVENT_ID_PLAYER_DAMAGED = HotBite::Engine::ECS::GetEventId<GamePlayerSystem>(0x00);
	static inline HotBite::Engine::ECS::EventId EVENT_ID_PLAYER_DEAD = HotBite::Engine::ECS::GetEventId<GamePlayerSystem>(0x01);

private:
	ECS::Coordinator* coordinator = nullptr;
	std::unique_ptr<GamePlayerData> spawn_data;
	ECS::Signature signature;
	ECS::EntityVector<GamePlayerData> players;
	ECS::Signature terrain_signature;
	ECS::EntityVector<TerrainData> terrains;
	reactphysics3d::Transform spawn_transform;
	CameraSystem::CameraData camera;
	ECS::Entity terrain = ECS::INVALID_ENTITY_ID;
	enum eAnimType { JUMP, RUN, WALK, IDLE };
	std::map<eAnimType, int> animation_count; //Used to avoid spurious animation transitions
	std::shared_ptr<PhysicsSystem> physics = nullptr;

public:

	//System needs the camera to move the player
	//oriented to camera
	void SetCamera(const CameraSystem::CameraData& c) {
		camera = c;
	}
	
	void OnRegister(ECS::Coordinator* c) override {
		this->coordinator = c;
		EventListener::Init(c);
		signature = GamePlayerData::GetSignature(c);
		terrain_signature = TerrainData::GetSignature(c);
		AddEventListener(GamePlayerSystem::EVENT_ID_PLAYER_DAMAGED, std::bind(&GamePlayerSystem::OnPlayerDamaged, this, std::placeholders::_1));
		AddEventListener(DXCore::EVENT_ID_MOUSE_LDOWN, std::bind(&GamePlayerSystem::OnAttack, this, std::placeholders::_1));
	}

	void OnEntityDestroyed(ECS::Entity entity) override {
		players.Remove(entity);
		terrains.Remove(entity);
	}

	void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override {
		if ((entity_signature & signature) == signature)
		{
			assert(players.GetData().empty() && "Only one player allowed");
			GamePlayerData pd{ coordinator, entity };
			spawn_transform = pd.physics->body->getTransform();
			players.Insert(entity, GamePlayerData{ coordinator, entity });
			AddEventListenerByEntity(Mesh::EVENT_ID_ANIMATION_END, entity, std::bind(&GamePlayerSystem::OnAnimationEnd, this, std::placeholders::_1));
		}
		else {
			players.Remove(entity);
		}

		if ((entity_signature & terrain_signature) == terrain_signature)
		{
			terrains.Insert(entity, TerrainData{ coordinator, entity });
		}
		else {
			terrains.Remove(entity);
		}
	}

	void OnPlayerDamaged(ECS::Event& e) {
		GamePlayerData* player = players.Get(e.GetEntity());
		if (player != nullptr) {
			if (player->creature->current_health <= 0.0f) {
				coordinator->SendEvent(this, player->base->id, GamePlayerSystem::EVENT_ID_PLAYER_DEAD);
			}
		}		
	}

	void Respawn() {
		std::lock_guard<std::recursive_mutex> l1(coordinator->GetSystem<RenderSystem>()->mutex);
		std::lock_guard<std::recursive_mutex> l2(physics_mutex);
		GamePlayerData& p = players.GetData().front();
		
		//Reset physics system contacts as we are teleporting the player
		coordinator->GetSystem<PhysicsSystem>()->ClearContact(p.base->id);
		p.physics->body->setTransform(spawn_transform);
		p.creature->current_health = p.creature->total_health;
		p.creature->on_fire = false;
		p.player->can_jump = false;
		p.creature->respawn_time = Scheduler::GetNanoSeconds();
		//Reset animation to avoid transition once respawned to start position
		p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_IDLE], 0.0f);
	}

	//Update player animation
	void Update(int64_t elapsed_nsec, int64_t total_nsec) {
		//We work with just 1 player
		std::lock_guard<std::recursive_mutex> l1(coordinator->GetSystem<RenderSystem>()->mutex);
		std::lock_guard<std::recursive_mutex> l2(physics_mutex);
		assert(players.GetData().size() == 1);
		GamePlayerData& p = players.GetData().front();
		if (p.creature->current_health <= 0.0f) {
			p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_DEAD], false, true);
			return;
		}
		
		p.mesh->SetAnimationDefaultTransitionTime(250.0f);
		
		Physics* physics = p.physics;
		GamePlayerComponent* player = p.player;

		float distance_to_terrain = FLT_MAX;
		for (const auto& terrain : terrains.GetConstData()) {
			//Calculate distance from player to terrain, if bigger than a limit, we are jumping
			//r0 is always the player center
			reactphysics3d::Vector3 r0 = { p.transform->position.x, p.transform->position.y + 100.0f, p.transform->position.z };
			//r1 is destination ray to terrain
			reactphysics3d::Vector3 r1 = { r0.x, p.transform->position.y - 100.0f, r0.z };
			reactphysics3d::RaycastInfo info;
			if (terrain.physics->body->raycast({ r0, r1 }, info)) {
				float dist = p.transform->position.y - info.worldPoint.y;
				if (dist < distance_to_terrain) {
					distance_to_terrain = dist;
				}
			}
		}
		bool contact_with_terrain = false;
		for (const auto& terrain : terrains.GetConstData()) {
			if (coordinator->GetSystem<PhysicsSystem>()->IsContact(p.base->id, terrain.base->id)) {
				contact_with_terrain = true;
				break;
			}
		}

		player->can_jump = (distance_to_terrain < 0.5f) && contact_with_terrain;
		p.player->can_attack = true;
		reactphysics3d::Vector3 speed = physics->body->getLinearVelocity();
		bool done = false;
		if (!done && player->is_attacking) {
			float3 dir = p.transform->position - camera.camera->world_position;
			dir.y = 0.0f;
			PlayerSystem::LookAt(&p, dir, 0.1f);
			p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_ATTACK], false, true);
			done = true;
		}
		if (!done && distance_to_terrain > 0.4f) {
			if (animation_count[JUMP]++ > 3) {
				p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_JUMP]);
				p.player->can_attack = false;
			}
			done = true;
		}
		else {
			animation_count[JUMP] = 0;
		}

		if (!done && (abs(speed.x) > 5.0f || abs(speed.z) > 5.0f)) {
			if (animation_count[RUN]++ > 3) {
				p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_RUN]);
			}
			done = true;
		}
		else {
			animation_count[RUN] = 0;
		}

		if (!done && (abs(speed.x) > 2.0f || abs(speed.z) > 2.0f ||
			(GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') | GetAsyncKeyState('D')) & 0x8000)) {
			if (animation_count[WALK]++ > 3) {
				p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_WALK]);
			}
			done = true;
		}
		else {
			animation_count[WALK] = 0;
		}
		if (!done) {
			if (animation_count[IDLE]++ > 3) {
				p.mesh->SetAnimation(p.creature->animations[CreatureAnimations::ANIM_IDLE]);
			}
		}
		else {
			animation_count[IDLE] = 0;
		}
	}

	void OnAttack(ECS::Event& ev) {
		GamePlayerData& player_data = players.GetData().front();
		if (player_data.creature->current_health > 0.0f &&
			player_data.player->can_attack &&
			!player_data.player->is_attacking) {
			PlayerSystem::Move(&player_data, {});
			player_data.player->is_attacking = true;
		}
	}

	void OnAnimationEnd(ECS::Event& ev) {
		GamePlayerData& player_data = players.GetData().front();
		if (ev.GetEntity() == player_data.base->id) {
			const std::string& name = ev.GetParam<const std::string&>(Mesh::EVENT_PARAM_ANIMATION_NAME);
			if (name == player_data.creature->animations[CreatureAnimations::ANIM_ATTACK]) {
				//Attack finished
				player_data.player->is_attacking = false;
			}
		}
	}

	//Check keyboard input to move the player
	void CheckKeys() {
		GamePlayerData& player_data = players.GetData().front();
		if (player_data.creature->current_health <= 0.0f) {
			return;
		}
		float constexpr move_amount = 0.8f;
		float3 move_dir = {};
		if (!player_data.player->is_attacking) {
			if (GetAsyncKeyState('W') & 0x8000)
			{
				move_dir = { 0.f, 0.f, move_amount };
			}
			if (GetAsyncKeyState('A') & 0x8000)
			{
				move_dir = { -move_amount, 0.f, 0.f };
			}
			if (GetAsyncKeyState('D') & 0x8000)
			{
				move_dir = { move_amount, 0.f, 0.f };
			}
			if (GetAsyncKeyState('S') & 0x8000)
			{
				move_dir = { 0.f, 0.f, -move_amount };
			}
			//Run with shift
			if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
				move_dir = MULT_F3_F(move_dir, 2.0f);
			}
			//Jump with space, can_jump is managed by PlayerSystem during update call.
			if (GetAsyncKeyState(VK_SPACE) & 0x8000 && player_data.player->can_jump == true)
			{
				PlayerSystem::Jump(&player_data, { 0.f, 100.f, 0.f });
			}
			if (LENGHT_SQUARE_F3(move_dir) > 0.0f) {
				//Set the movement to camera orientation
				vector3d md = XMLoadFloat3(&move_dir);
				md = XMVector3Rotate(md, camera.camera->xm_rotation);
				XMStoreFloat3(&move_dir, md);
				move_dir.y = 0.0f;
				PlayerSystem::Move(&player_data, move_dir);
			}
		}
	}
};
