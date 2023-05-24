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


/**
 * This system is used to manage fire entities that can damage our player.
 */
class FireSystem : public ECS::System, public ECS::EventListener {
private:
	// FireSystem::PlayerData contains the components of a player entity that are used
	// by this system. We could reuse GamePlayerSystem::GamePlayerData container, but we
	// declare this one to show how to create a new one.
	struct BurnableCreatureData {
		HotBite::Engine::Components::Base* base = nullptr;
		HotBite::Engine::Components::Physics* physics = nullptr;
		HotBite::Engine::Components::Mesh* mesh = nullptr;
		HotBite::Engine::Components::Particles* particles = nullptr;
		CreatureComponent* creature = nullptr;

		BurnableCreatureData() = delete;

		BurnableCreatureData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) {
			base = c->GetComponentPtr<HotBite::Engine::Components::Base>(entity);
			physics = c->GetComponentPtr<HotBite::Engine::Components::Physics>(entity);
			mesh = c->GetComponentPtr<HotBite::Engine::Components::Mesh>(entity);
			particles = c->GetComponentPtr<HotBite::Engine::Components::Particles>(entity);
			creature = c->GetComponentPtr<CreatureComponent>(entity);
		}

		static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
			HotBite::Engine::ECS::Signature signature;
			signature.set(c->GetComponentType<HotBite::Engine::Components::Base>(), true);
			signature.set(c->GetComponentType<HotBite::Engine::Components::Physics>(), true);
			signature.set(c->GetComponentType<HotBite::Engine::Components::Mesh>(), true);
			signature.set(c->GetComponentType<HotBite::Engine::Components::Particles>(), true);
			signature.set(c->GetComponentType<CreatureComponent>(), true);
			return signature;
		}
	};

	// FireData contains the components of the entities that can burn other entities, like fireballs or lava.
	struct FireData {
		HotBite::Engine::Components::Base* base = nullptr;
		HotBite::Engine::Components::Physics* physics = nullptr;
		FireComponent* fire = nullptr;

		FireData() = delete;

		FireData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) {
			base = c->GetComponentPtr<HotBite::Engine::Components::Base>(entity);
			physics = c->GetComponentPtr<HotBite::Engine::Components::Physics>(entity);
			fire = c->GetComponentPtr<FireComponent>(entity);
		}

		static  HotBite::Engine::ECS::Signature GetSignature(HotBite::Engine::ECS::Coordinator* c) {
			HotBite::Engine::ECS::Signature signature;
			signature.set(c->GetComponentType<HotBite::Engine::Components::Base>(), true);
			signature.set(c->GetComponentType<HotBite::Engine::Components::Physics>(), true);
			signature.set(c->GetComponentType<FireComponent>(), true);
			return signature;
		}
	};

	//Here we store a vector with the game players and another with the fire entities.
	ECS::Signature creature_signature;
	ECS::EntityVector<BurnableCreatureData> creatures;
	ECS::Signature fire_signature;
	ECS::EntityVector<FireData> fires;

	//Here we store the coordinator of the game.
	ECS::Coordinator* coordinator = nullptr;

	//Handler of the timer, so we can remove it when object is destroyed.
	Scheduler::TimerId check_timer_id = Scheduler::INVALID_TIMER_ID;

public:

	virtual ~FireSystem() {
		if (check_timer_id != Scheduler::INVALID_TIMER_ID) {
			Scheduler::Get(DXCore::MAIN_THREAD)->RemoveTimer(check_timer_id);
		}
	}

	//Called by coordinator when the system is registered in it.
	void OnRegister(ECS::Coordinator* c) override {
		this->coordinator = c;
		ECS::EventListener::Init(c);
		creature_signature = BurnableCreatureData::GetSignature(c);
		fire_signature = FireData::GetSignature(c);
		check_timer_id = Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(MSEC_TO_NSEC(100), std::bind(&FireSystem::OnCheck, this, std::placeholders::_1));
	}

	//Called by coordinator every time an entity is destroyed.
	void OnEntityDestroyed(Entity entity) override {
		//Remove can be called always, it only perform action when the entity is in the vector.
		creatures.Remove(entity);
		fires.Remove(entity);
	}

	//Called by coordinator every time the entity signature is created or changed.
	//This is, a component has been added or removed to the entity and NotifySignatureChange is called.
	//NotifySignatureChange is not called internally automatically to avoid spamming when we add/remove several components.
	//Instead, we call it manually when we have finished adding/removing components.
	void OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) override {
		if ((entity_signature & creature_signature) == creature_signature)
		{
			BurnableCreatureData pd{ coordinator, entity };
			creatures.Insert(entity, pd);
		}
		else {
			creatures.Remove(entity);
		}
		if ((entity_signature & fire_signature) == fire_signature)
		{
			fires.Insert(entity, FireData{ coordinator, entity });
		}
		else {
			fires.Remove(entity);
		}
	}

	//Called by the timer every 100ms.
	bool OnCheck(const Scheduler::TimerData& td) {
		//Lock the physics world while we check fire contacts
		physics_mutex.lock();
		//Get current scene collision data from physics system
		const Systems::PhysicsSystem::CollisionData& collision_data = coordinator->GetSystem<Systems::PhysicsSystem>()->GetCollisions();

		for (auto& c : creatures.GetData()) {
			auto creature_contacts = collision_data.find(c.physics->body);
			if (creature_contacts != collision_data.end()) {
				//Check if player is in contact with fire
				for (const auto& fire : fires.GetConstData()) {
					auto fire_contacts = creature_contacts->second.find(fire.physics->body);
					if (fire_contacts != creature_contacts->second.end()) {
						c.creature->fire_start = Scheduler::GetNanoSeconds();
						if (!c.creature->on_fire && c.creature->current_health > 0.0f) {
							//Burn player during fire duration
							c.creature->on_fire = true;
							Particles& p = coordinator->GetComponent<Particles>(c.base->id);
							auto part = p.data.Get("1_sparks");
							if (part != nullptr) {
								(*part)->density = 0.5f;
							}
							part = p.data.Get("2_fire_core");
							if (part != nullptr) {
								(*part)->density = 0.5f;
							}
							part = p.data.Get("3_smoke");
							if (part != nullptr) {
								(*part)->density = 0.05f;
							}
							Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(1000000000 / 60, [this, c, fire](const Scheduler::TimerData& t) {
								int64_t now = Scheduler::GetNanoSeconds();
								//Stop fire if player is respawning or fire duration elapsed
								bool ret = ((now - c.creature->fire_start) < fire.fire->duration) && c.creature->current_health > 0.0f && ((now - c.creature->respawn_time) > SEC_TO_NSEC(1));
								if (ret) {
									c.creature->current_health -= fire.fire->dps / 60.0f;
									coordinator->SendEvent(this, c.base->id, GamePlayerSystem::EVENT_ID_PLAYER_DAMAGED);
								} else {
									for (auto& particle : c.particles->data.GetData()) {
										particle->density = 0.0f;
									}
									c.creature->on_fire = false;
								}
								return ret;
								});
							break;
						}
					}
				}
			}
		}
		physics_mutex.unlock();
		return true;
	}
};