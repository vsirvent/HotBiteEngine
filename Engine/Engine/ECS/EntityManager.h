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
#include <Core/Utils.h>
#include <array>
#include <cassert>
#include <queue>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			class EntityManager
			{
			public:
				EntityManager()
				{
					for (Entity entity = 0; entity < MAX_ENTITIES; ++entity)
					{
						available_entities.push(entity);
					}
				}

				Entity CreateEntity(const std::string& name)
				{
					assert(living_entity_count < MAX_ENTITIES && "Too many entities in existence.");
					Entity id = available_entities.front();
					available_entities.pop();
					++living_entity_count;
					assert(entity_by_name.find(name) == entity_by_name.end() && "Entity already exists.");
					entity_by_name[name] = id;
					name_by_entity[id] = name;
					return id;
				}

				Entity GetEntityByName(const std::string& name) const
				{
					Entity id = INVALID_ENTITY_ID;
					auto it = entity_by_name.find(name);
					if (it != entity_by_name.end()) {
						id = it->second;
					}
					return id;
				}

				std::list<Entity> GetEntitiesByName(const std::string& name) const
				{
					std::list<Entity> ret;
					if (name.find("*") == std::string::npos) {
						Entity e = GetEntityByName(name);
						if (e != INVALID_ENTITY_ID) {
							ret.push_back(e);
						}
					}
					else {
						for (auto& e : entity_by_name) {
							if (Core::Match(e.first, name)) {
								ret.push_back(e.second);
							}
						}
					}
					return ret;
				}

				const std::unordered_map<std::string, Entity>& GetEntities() const {
					return entity_by_name;
				}

				void DestroyEntity(Entity entity)
				{
					assert(entity < MAX_ENTITIES && "Entity out of range.");
					signatures[entity].reset();
					available_entities.push(entity);
					--living_entity_count;
					auto it = name_by_entity.find(entity);
					assert(it != name_by_entity.end() && "Unknown entity.");
					entity_by_name.erase(entity_by_name.find(it->second));
					name_by_entity.erase(it);
				}

				void SetSignature(Entity entity, Signature signature)
				{
					assert(entity < MAX_ENTITIES && "Entity out of range.");
					signatures[entity] = signature;
				}

				Signature GetSignature(Entity entity)
				{
					assert(entity < MAX_ENTITIES && "Entity out of range.");
					return signatures[entity];
				}

			private:
				std::unordered_map<std::string, Entity> entity_by_name;
				std::unordered_map<Entity, std::string> name_by_entity;
				std::queue<Entity> available_entities;
				std::array<Signature, MAX_ENTITIES> signatures;
				uint32_t living_entity_count{};
			};
		}
	}
}