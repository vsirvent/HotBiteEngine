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

#include "Types.h"
#include <any>
#include <unordered_map>
#include <list>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			// Events
			using EventId = uint32_t;
			using ParamId = uint32_t;

			class IEventSender {};
			
			class Event
			{
			public:
				Event() {}
				Event(IEventSender* s, Entity e, EventId t)
					: sender(s), entity(e), type(t)
				{}

				Event(IEventSender* s, EventId t)
					: sender(s), entity(INVALID_ENTITY_ID), type(t)
				{}

				void SetSender(IEventSender* sender) {
					sender = sender;
				}

				template<typename T>
				void SetParam(ParamId id, const T& value)
				{
					data[id] = value;
				}

				template<typename T>
				void EmplaceParam(ParamId id, T&& value)
				{
					data[id] = std::forward<T>(value);
				}

				template<typename T>
				auto GetParam(ParamId id)
				{
					return std::any_cast<T>(data[id]);
				}
				
				EventId GetType() const
				{
					return type;
				}

				void SetType(EventId t) {
					type = t;
				}

				Entity GetEntity() const
				{
					return entity;
				}

				void SetEntity(Entity e) {
					entity = e;
				}

				IEventSender* GetSender() const {
					return sender;
				}

			private:
				IEventSender* sender = nullptr;
				Entity entity = INVALID_ENTITY_ID;
				EventId type;
				std::unordered_map<ParamId, std::any> data;
			};

			template <class T>
			EventId GetEventId(int id) {
				return (EventId)(typeid(T).hash_code() & 0xffff0000 | id & 0x0000ffff);
			}
		}
	}
}