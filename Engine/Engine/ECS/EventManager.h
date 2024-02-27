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

#include "Event.h"
#include "Types.h"
#include <functional>
#include <list>
#include <unordered_map>

namespace std {
	template <> struct hash<std::pair<HotBite::Engine::ECS::EventId, HotBite::Engine::ECS::Entity>>
	{
		size_t operator()(std::pair<HotBite::Engine::ECS::EventId, HotBite::Engine::ECS::Entity> const& p) const
		{
			return hash<int>()(p.first) ^ (hash<int>()(p.second) << 16);
		}
	};
}

namespace HotBite {
	namespace Engine {
		namespace ECS {
			struct EventListenerId {
				uint32_t listener_id;
				EventId ev_id;
				Entity e = INVALID_ENTITY_ID;
			};


			class EventManager
			{
			public:
				EventManager() {
					for (int i = 0; i < MAX_LISTENERS; ++i) {
						listener_ids.push_back(i);
					}
				}
				EventListenerId AddListener(EventId eventId, std::function<void(Event&)> const&& listener)
				{
					assert(!listener_ids.empty() && "Maximum listeners reached");
					uint32_t listener_id = listener_ids.front();
					listener_ids.pop_front();
					EventListenerId id{ listener_id, eventId };
					assert(listener_by_id.find(id.listener_id) == listener_by_id.end());
					EventPair key(eventId, ECS::INVALID_ENTITY_ID);
					listener_by_id[id.listener_id] = key;
					listeners[key].emplace_back(listener);
					return id;
				}

				EventListenerId AddListenerByEntity(EventId eventId, Entity e, std::function<void(Event&)> const&& listener)
				{
					assert(!listener_ids.empty() && "Maximum listeners reached");
					uint32_t listener_id = listener_ids.front();
					listener_ids.pop_front();
					EventListenerId id{ listener_id, eventId, e };
					assert(listener_by_id.find(id.listener_id) == listener_by_id.end());
					EventPair key(eventId, e);
					listener_by_id[id.listener_id] = key;
					listeners_by_entity[key].emplace_back(listener);
					return id;
				}

				void RemoveListener(const EventListenerId& id)
				{
					bool removed = false;
					auto it = listener_by_id.find(id.listener_id);
					if (it != listener_by_id.end()) {
						if (id.e != INVALID_ENTITY_ID) {
							auto it2 = listeners_by_entity.find(it->second);
							if (it2 != listeners_by_entity.end()) {
								listeners_by_entity.erase(it2);
								removed = true;
							}
						}
						else {
							auto it2 = listeners.find(it->second);
							if (it2 != listeners.end()) {
								listeners.erase(it2);
								removed = true;
							}
						}
						//assert(removed == true);
						listener_by_id.erase(it);
						listener_ids.push_back(id.listener_id);
					}
					//assert(removed == true);
				}

				void SendEvent(Event& event)
				{
					uint32_t type = event.GetType();
					Entity e = event.GetEntity();
					EventPair nid(type, ECS::INVALID_ENTITY_ID);
					auto it = listeners.find(nid);
					if (it != listeners.end()) {
						for (const auto& listener : it->second)
						{
							listener(event);
						}
					}
					if (e != INVALID_ENTITY_ID) {
						EventPair eid(type, e);
						auto listeners_by_type_entity = listeners_by_entity.find(eid);
						if (listeners_by_type_entity != listeners_by_entity.end()) {
							for (const auto& listener : listeners_by_type_entity->second) {
								listener(event);
							}
						}
					}
				}

				void SendEvent(Event&& event)
				{
					uint32_t type = event.GetType();
					Entity e = event.GetEntity();
					EventPair nid(type, ECS::INVALID_ENTITY_ID);
					auto it = listeners.find(nid);
					if (it != listeners.end()) {
						for (const auto& listener : it->second)
						{
							listener(event);
						}
					}
					if (e != INVALID_ENTITY_ID) {
						EventPair eid(type, e);
						auto listeners_by_type_entity = listeners_by_entity.find(eid);
						if (listeners_by_type_entity != listeners_by_entity.end()) {
							for (const auto& listener : listeners_by_type_entity->second) {
								listener(event);
							}
						}
					}
				}

				void SendEvent(IEventSender* sender, Entity entity, EventId type)
				{
					SendEvent(Event{ sender, entity, type });
				}

			private:
				static const int MAX_LISTENERS = 5000;
				using EventPair = std::pair<EventId, Entity>;
				std::list<uint32_t> listener_ids;
				std::unordered_map<EventPair, std::vector<std::function<void(Event&)>>> listeners_by_entity;
				std::unordered_map<EventPair, std::vector<std::function<void(Event&)>>> listeners;
				std::unordered_map<uint32_t, EventPair> listener_by_id;
			};
		}
	}
}