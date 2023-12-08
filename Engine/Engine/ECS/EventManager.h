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
					listener_by_id[id.listener_id] = &(listeners[eventId].emplace_back(listener));
					return id;
				}

				EventListenerId AddListenerByEntity(EventId eventId, Entity e, std::function<void(Event&)> const&& listener)
				{
					assert(!listener_ids.empty() && "Maximum listeners reached");
					uint32_t listener_id = listener_ids.front();
					listener_ids.pop_front();
					EventListenerId id{ listener_id, eventId, e };
					listener_by_id[id.listener_id] = &(listeners_by_entity[std::pair<EventId, Entity>(eventId, e)].emplace_back(listener));
					return id;
				}

				void RemoveListener(const EventListenerId& id)
				{
					auto it = listener_by_id.find(id.listener_id);
					if (it != listener_by_id.end() && it->first != 0) {
						for (auto it2 = listeners[id.ev_id].begin(); it2 != listeners[id.ev_id].end(); ++it2) {
							if (&(*it2) == it->second) {
								if (id.e != INVALID_ENTITY_ID) {
									listeners_by_entity[std::pair<uint32_t, Entity>(id.ev_id, id.e)].erase(it2);
								}
								else {
									listeners[id.ev_id].erase(it2);
								}
							}
						}
						listener_by_id.erase(it);
						listener_ids.push_back(id.ev_id);
					}
				}

				void SendEvent(Event& event)
				{
					uint32_t type = event.GetType();
					Entity e = event.GetEntity();
					auto& listeners_by_type = listeners[type];
					for (const auto& listener : listeners_by_type)
					{
						listener(event);
					}
					if (e != INVALID_ENTITY_ID) {
						auto listeners_by_type_entity = listeners_by_entity.find(std::pair<EventId, Entity>(type, e));
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
					auto& listeners_by_type = listeners[type];
					for (const auto& listener : listeners_by_type)
					{
						listener(event);
					}
					if (e != INVALID_ENTITY_ID) {
						auto listeners_by_type_entity = listeners_by_entity.find(std::pair<EventId, Entity>(type, e));
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
				std::list<uint32_t> listener_ids;
				std::unordered_map<std::pair<EventId, Entity>, std::vector<std::function<void(Event&)>>> listeners_by_entity;
				std::unordered_map<EventId, std::vector<std::function<void(Event&)>>> listeners;
				std::unordered_map<uint32_t, std::function<void(Event&)>*> listener_by_id;
			};
		}
	}
}