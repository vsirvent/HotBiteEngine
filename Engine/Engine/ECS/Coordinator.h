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

#include "ComponentManager.h"
#include "EntityManager.h"
#include "EventManager.h"
#include "SystemManager.h"
#include "Types.h"
#include <memory>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			class Coordinator: public IEventSender
			{
			public:
				static inline EventId EVENT_ID_ENTITY_CREATED = GetEventId<Coordinator>(0x00);
				static inline EventId EVENT_ID_ENTITY_REMOVED = GetEventId<Coordinator>(0x01);
				static inline EventId EVENT_ID_ENTITY_CHANGED = GetEventId<Coordinator>(0x02);

				Coordinator() = default;

				~Coordinator() {
					system_manager = nullptr;
					component_manager = nullptr;
					entity_manager = nullptr;
					event_manager = nullptr;
				}
				void Init()
				{
					component_manager = std::make_unique<ComponentManager>();
					entity_manager = std::make_unique<EntityManager>();
					event_manager = std::make_unique<EventManager>();
					system_manager = std::make_unique<SystemManager>();
				}

				// Entity methods
				Entity CreateEntity(const std::string& name)
				{
					Entity e = entity_manager->CreateEntity(name);
					event_manager->SendEvent(this, e, EVENT_ID_ENTITY_CREATED);
					return e;
				}

				void ChangeEntityName(const std::string& old_name, const std::string& new_name)
				{
					entity_manager->ChangeEntityName(old_name, new_name);
				}

				void DestroyEntity(Entity entity)
				{
					entity_manager->DestroyEntity(entity);
					system_manager->EntityDestroyed(entity);
					for (Entity e : component_manager->EntityDestroyed(entity)) {
						//We need to refresh all the entities that 
						//are internally moved from one index to another
						//so the systems can refresh cached references to the components
						system_manager->EntitySignatureChanged(e, entity_manager->GetSignature(e));
					}
					event_manager->SendEvent(this, entity, EVENT_ID_ENTITY_REMOVED);
				}

				Entity GetEntityByName(const std::string& name) const {
					return entity_manager->GetEntityByName(name);
				}

				std::list<Entity> GetEntitiesByName(const std::string& name) const {
					return entity_manager->GetEntitiesByName(name);
				}

				const std::unordered_map<std::string, Entity>& GetEntites() const {
					return entity_manager->GetEntities();
				}


				Signature GetEntitySignature(Entity e) {
					return entity_manager->GetSignature(e);
				}

				// Component methods
				template<typename T>
				void RegisterComponent()
				{
					component_manager->RegisterComponent<T>();
				}

				template<typename T>
				void AddComponent(Entity entity, const T& component)
				{
					component_manager->AddComponent<T>(entity, component);
					auto signature = entity_manager->GetSignature(entity);
					signature.set(component_manager->GetComponentType<T>(), true);
					entity_manager->SetSignature(entity, signature);
				}

				template<typename T>
				void AddComponentIfNotExists(Entity entity, const T& component)
				{
					if (!component_manager->ContainsComponent<T>(entity)) {
						component_manager->AddComponent<T>(entity, component);
						auto signature = entity_manager->GetSignature(entity);
						signature.set(component_manager->GetComponentType<T>(), true);
						entity_manager->SetSignature(entity, signature);
					}
				}

				template<typename T>
				void AddComponent(Entity entity)
				{
					component_manager->EmplaceComponent<T>(entity, T{});
					auto signature = entity_manager->GetSignature(entity);
					signature.set(component_manager->GetComponentType<T>(), true);
					entity_manager->SetSignature(entity, signature);
				}

				template<typename T>
				void EmplaceComponent(Entity entity, T&& component)
				{
					component_manager->EmplaceComponent<T>(entity, std::forward<T>(component));
					auto signature = entity_manager->GetSignature(entity);
					signature.set(component_manager->GetComponentType<T>(), true);
					entity_manager->SetSignature(entity, signature);
				}

				void NotifySignatureChange(Entity entity) {
					auto signature = entity_manager->GetSignature(entity);
					system_manager->EntitySignatureChanged(entity, signature);
					event_manager->SendEvent(this, entity, EVENT_ID_ENTITY_CHANGED);
				}

				template<typename T>
				void RemoveComponent(Entity entity)
				{
					Entity e = component_manager->RemoveComponent<T>(entity);
					//We need to refresh all the entities that 
					//are internally moved from one index to another
					//so the systems can refresh cached references to the components
					system_manager->EntitySignatureChanged(e, entity_manager->GetSignature(e));

					auto signature = entity_manager->GetSignature(entity);
					signature.set(component_manager->GetComponentType<T>(), false);
					entity_manager->SetSignature(entity, signature);
					system_manager->EntitySignatureChanged(entity, signature);

				}

				template<typename T>
				bool ContainsComponent(Entity entity)
				{
					return component_manager->ContainsComponent<T>(entity);
				}

				template<typename T>
				const T& GetConstComponent(Entity entity) const 
				{
					return component_manager->GetConstComponent<T>(entity);
				}

				template<typename T>
				T& GetComponent(Entity entity)
				{
					return component_manager->GetComponent<T>(entity);
				}

				template<typename T>
				T* GetComponentPtr(Entity entity)
				{
					return &(component_manager->GetComponent<T>(entity));
				}
			
				template<typename T>
				std::shared_ptr<ComponentArray<T>> GetComponents()
				{
					return component_manager->GetComponents<T>();
				}

				template<typename T>
				ComponentType GetComponentType()
				{
					return component_manager->GetComponentType<T>();
				}

				// System methods
				template<typename T>
				std::shared_ptr<T> RegisterSystem()
				{
					std::shared_ptr<T> system = system_manager->RegisterSystem<T>();
					system->OnRegister(this);
					return system;
				}
				template<typename T>
				std::shared_ptr<T> GetSystem()
				{
					return system_manager->GetSystem<T>();
				}

				// Event methods
				EventListenerId AddEventListener(EventId eventId, std::function<void(Event&)> const&& listener)
				{
					return event_manager->AddListener(eventId, std::forward<std::function<void(Event&)> const>(listener));
				}

				EventListenerId AddEventListenerByEntity(EventId eventId, Entity e, std::function<void(Event&)> const&& listener)
				{
					return event_manager->AddListenerByEntity(eventId, e, std::forward<std::function<void(Event&)> const>(listener));
				}

				void RemoveEventListener(const EventListenerId& listener_id)
				{
					event_manager->RemoveListener(listener_id);
				}

				void SendEvent(Event& event)
				{
					event_manager->SendEvent(event);
				}

				void SendEvent(IEventSender* sender, EventId type)
				{
					event_manager->SendEvent(sender, INVALID_ENTITY_ID, type);
				}

				void SendEvent(IEventSender* sender, Entity entity, EventId type)
				{
					event_manager->SendEvent(sender, entity, type);
				}

			private:
				std::unique_ptr<ComponentManager> component_manager;
				std::unique_ptr<EntityManager> entity_manager;
				std::unique_ptr<EventManager> event_manager;
				std::unique_ptr<SystemManager> system_manager;
			};

			class EventListener {
			private:
				std::list<EventListenerId> ev_list_ids;
				Coordinator* event_coordinator = nullptr;
			
			public:
				EventListener() {}

				void Init(Coordinator* c) {
					event_coordinator = c;
				}

				void Reset() {
					if (event_coordinator != nullptr) {
						for (auto& id : ev_list_ids) {
							event_coordinator->RemoveEventListener(id);
						}
						event_coordinator = nullptr;
					}
				}

				virtual ~EventListener() {
					Reset();
				}

				// Event methods
				EventListenerId AddEventListener(EventId eventId, std::function<void(Event&)> const&& listener)
				{
					EventListenerId id = event_coordinator->AddEventListener(eventId, std::forward<std::function<void(Event&)> const>(listener));
					ev_list_ids.push_back(id);
					return id;
				}

				// Event methods
				EventListenerId AddEventListenerByEntity(EventId eventId, Entity e, std::function<void(Event&)> const&& listener)
				{
					EventListenerId id = event_coordinator->AddEventListenerByEntity(eventId, e, std::forward<std::function<void(Event&)> const>(listener));
					ev_list_ids.push_back(id);
					return id;
				}
			};
		}
	}
}
