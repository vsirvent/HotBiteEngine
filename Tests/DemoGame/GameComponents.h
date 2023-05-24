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

#include <Components\Base.h>
#include <Systems\PlayerSystem.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Components;

enum CreatureAnimations {
	ANIM_IDLE,
	ANIM_ATTACK,
	ANIM_WALK,
	ANIM_RUN,
	ANIM_JUMP,
	ANIM_DEAD
};

struct EnemyComponent {
	float speed = 0.0f; // The enemy movement speed
	float range = 0.0f; // The detection range
	float damage = 0.0f; // The damage per hit
	float attack_range = 0.0f; // The attack range
	bool is_attacking = false;
	bool damage_done = false; // If enemy has done alredy damage during the attacke period
	int attack_frame = 0; // The moment when the enemy damages during animation
	int64_t attack_period = 0; // The time between attacks
	int64_t last_attack_time = 0; // The last time the enemy attacked
	int64_t last_idle_time = 0; // The last time the enemy was idle to do random movements
	float3 random_move_dir = {}; // Random movement direction
};

struct CreatureComponent {
	float current_health = 100.0f;
	float total_health = 100.0f;
	int64_t fire_start = 0;
	bool on_fire = false;
	int64_t respawn_time = 0;
	std::string mesh_name;
	std::string material_name;
	float3 scale = { 1.0f, 1.0f, 1.0f };
	std::map<CreatureAnimations, std::string> animations; // The animations of a creature
};

struct GamePlayerComponent {
	bool can_jump = false;
	bool can_attack = false;
	bool is_attacking = false;
	int attack_frame = 0; // The moment when the enemy damages during animation
	int64_t last_attack_time = 0; // The last time the enemy attacked
	float damage = 0.0f; // The damage per hit
};

struct FireComponent {
	int64_t duration = 0;
	float dps = 0.0f;
};

struct TerrainComponent {};

struct TerrainData {
	Components::Base* base;
	Components::Transform* transform;
	Components::Physics* physics;

	TerrainData(ECS::Coordinator* c, ECS::Entity entity) {
		base = c->GetComponentPtr<Components::Base>(entity);
		transform = c->GetComponentPtr<Components::Transform>(entity);
		physics = c->GetComponentPtr<Components::Physics>(entity);
	}

	static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
		HotBite::Engine::ECS::Signature signature;
		signature.set(c->GetComponentType<Components::Base>(), true);
		signature.set(c->GetComponentType<Components::Transform>(), true);
		signature.set(c->GetComponentType<Components::Physics>(), true);
		signature.set(c->GetComponentType<TerrainComponent>(), true);
		return signature;
	}
};

struct CreatureData : public Systems::PlayerSystem::PlayerData {
	CreatureComponent* creature = nullptr;

	CreatureData() = delete;

	CreatureData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) :
		Systems::PlayerSystem::PlayerData(c, entity) {
		creature = c->GetComponentPtr<CreatureComponent>(entity);
	}

	static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
		HotBite::Engine::ECS::Signature signature = Systems::PlayerSystem::PlayerData::GetSignature(c);
		signature.set(c->GetComponentType<CreatureComponent>(), true);
		return signature;
	}
};

struct GameEnemyData : public CreatureData {
	EnemyComponent* enemy = nullptr;

	GameEnemyData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) :
		CreatureData(c, entity) {
		enemy = c->GetComponentPtr<EnemyComponent>(entity);
	}

	static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
		HotBite::Engine::ECS::Signature signature = CreatureData::GetSignature(c);
		signature.set(c->GetComponentType<EnemyComponent>(), true);
		return signature;
	}
};

struct GamePlayerData : public CreatureData {
	GamePlayerComponent* player = nullptr;

	GamePlayerData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) :
		CreatureData(c, entity) {
		player = c->GetComponentPtr<GamePlayerComponent>(entity);
	}

	static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
		HotBite::Engine::ECS::Signature signature = CreatureData::GetSignature(c);
		signature.set(c->GetComponentType<GamePlayerComponent>(), true);
		return signature;
	}
};