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
#include "GameComponents.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;


class EnemySystem : public Systems::PlayerSystem, public EventListener {
private:
			
	//Here we store a vector with the game players and another with the fire entities.
	ECS::Signature enemy_signature;
	ECS::EntityVector<GameEnemyData> enemies;
	ECS::Signature player_signature;
	ECS::EntityVector<GamePlayerData> players;

	//Here we store the coordinator of the game.
	ECS::Coordinator* coordinator = nullptr;

public:

	//Called by coordinator when the system is registered in it.
	void OnRegister(ECS::Coordinator* c) override {
		this->coordinator = c;
		EventListener::Init(c);
		enemy_signature = GameEnemyData::GetSignature(c);
		player_signature = GamePlayerData::GetSignature(c);
	}

	//Called by coordinator every time an entity is destroyed.
	void OnEntityDestroyed(Entity entity) override {
		//Remove can be called always, it only perform action when the entity is in the vector.
		players.Remove(entity);
		enemies.Remove(entity);
	}

	//Called by coordinator every time the entity signature is created or changed.
	//This is, a component has been added or removed to the entity and NotifySignatureChange is called.
	//NotifySignatureChange is not called internally automatically to avoid spamming when we add/remove several components.
	//Instead, we call it manually when we have finished adding/removing components.
	void OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) override {
		if ((entity_signature & enemy_signature) == enemy_signature)
		{
			enemies.Insert(entity, GameEnemyData{ coordinator, entity });
			AddEventListenerByEntity(Mesh::EVENT_ID_ANIMATION_END, entity, std::bind(&EnemySystem::OnAnimationEnd, this, std::placeholders::_1));
		}
		else {
			enemies.Remove(entity);
		}
		if ((entity_signature & player_signature) == player_signature)
		{
			players.Insert(entity, GamePlayerData{ coordinator, entity });
		}
		else {
			players.Remove(entity);
		}
	}

	void OnAnimationEnd(Event& ev) {
		ECS::Entity e = ev.GetEntity();
		GameEnemyData* enemy = enemies.Get(e);
		if (enemy) {
			if (enemy->mesh->GetCurrentAnimationName() == enemy->creature->animations[CreatureAnimations::ANIM_ATTACK]) {
				enemy->enemy->is_attacking = false;
			}
		}
	}

	//Called by the timer every 100ms.
	void Update(int64_t elapsed_nsec, int64_t total_nsec) {
		//As this timer is executed by DXCore::MAIN_THREAD, don't need to take mutex of system renderer
		//Lock the physics world while we check fire contacts
		std::lock_guard<std::recursive_mutex> l2(physics_mutex); 
		std::shared_ptr<Systems::PhysicsSystem> physics_system = coordinator->GetSystem<Systems::PhysicsSystem>();
		int64_t now = Scheduler::GetNanoSeconds();

		//If enemy detects a player, move towards the player and attack him
		for (auto& e : enemies.GetData()) {
			for (auto& p : players.GetData()) {
				bool done = false;
				if (e.creature->current_health > 0.0f) {
					if (p.creature->current_health > 0.0f) {
						float3 dir = SUB_F3_F3(p.transform->position, e.transform->position);
						float distance = LENGHT_SQUARE_F3(dir);
						int64_t attack_time = (now - e.enemy->last_attack_time);
						//Divide in enemy is attacking (check if damage is done) or not (move or attack)
						if (e.enemy->is_attacking) {
							if (distance < e.enemy->attack_range &&
								e.enemy->damage_done == false &&
								e.mesh->GetCurrentFrame() == e.enemy->attack_frame) {
								//Do damage in the exact attack animation frame
								e.enemy->damage_done = true;
								p.creature->current_health -= e.enemy->damage;
								coordinator->SendEvent(this, p.base->id, GamePlayerSystem::EVENT_ID_PLAYER_DAMAGED);
							}
							//Don't move during attack
							e.physics->body->setLinearVelocity({});
							done = true;
						}
						else {
							//Attach if in attack range and can attack		
							if (attack_time > e.enemy->attack_period && distance < e.enemy->attack_range) {
								//Can attack
								e.enemy->last_attack_time = now;
								e.enemy->damage_done = false;
								e.enemy->is_attacking = true;
								e.mesh->SetAnimation(e.creature->animations[CreatureAnimations::ANIM_ATTACK],
									false, true, -1.0f, 1.0f, true);
								done = true;
							} else if ( distance < e.enemy->range && distance > e.enemy->attack_range * 0.8f) {
								//If we can't attack, move towards enemy if distance is not too close and is detected
								dir.y = 0.0f;
								PlayerSystem::Move(&e, UNIT_F3(dir) * e.enemy->speed, 0.01f);
								done = true;
							}
						}
					}
					if (!done) {
						//Nothing to do, move randomly every 5 seconds
						int64_t idle_time = now - e.enemy->last_idle_time;
						float r = getRandomNumber(0.0f, 1.0f);
						if (idle_time > SEC_TO_NSEC(5)) {
							e.enemy->last_idle_time = now;
							//Randomly decide if move, stop or just continue what the enemy was doing...
							if (r > 0.7f) {
								e.enemy->random_move_dir = float3{ getRandomNumber(-1.0f, 1.0f), 0.0f, getRandomNumber(-1.0f, 1.0f) };
							}
							else if (r > 0.4f) {
								e.enemy->random_move_dir = {};
							}
						}
						if (LENGHT_F3(e.enemy->random_move_dir) > 0.0f) {
							PlayerSystem::Move(&e, UNIT_F3(e.enemy->random_move_dir) * e.enemy->speed, 0.08f);
						}
					}
				}
			}
		}

		//Check enemy animation
		for (auto& e : enemies.GetData()) {	
			if (e.creature->current_health <= 0.0f) {
				e.mesh->SetAnimation(e.creature->animations[CreatureAnimations::ANIM_DEAD], false, true);
				continue;
			}
			//If we are attacking, do nothing
			if (e.enemy->is_attacking) {
				continue;
			}
			//If we are moving
			Physics* physics = e.physics;
			reactphysics3d::Vector3 speed = physics->body->getLinearVelocity();

			if ((abs(speed.x) > 0.0f || abs(speed.z) > 0.0f)) {
				e.mesh->SetAnimation(e.creature->animations[CreatureAnimations::ANIM_WALK]);
				continue;
			}
			//If none of previous, enemy is idle
			e.mesh->SetAnimation(e.creature->animations[CreatureAnimations::ANIM_IDLE]);
		}
	}
};