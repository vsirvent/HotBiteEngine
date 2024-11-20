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
#include <array>
#include <cassert>
#include <unordered_map>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			class IComponentArray
			{
			public:
				virtual ~IComponentArray() = default;
				virtual Entity EntityDestroyed(Entity entity) = 0;
			};


			template<typename T>
			class ComponentArray : public IComponentArray
			{
			public:
				void InsertData(Entity entity, const T& component)
				{
					assert(entity_to_index_map.find(entity) == entity_to_index_map.end() && "Component added to same entity more than once.");
					// Put new entry at end
					size_t new_index = component_array.size();
					entity_to_index_map[entity] = new_index;
					index_to_entity_map[new_index] = entity;
					component_array.push_back(component);
				}

				std::vector<T>& Array() {
					return component_array;
				}

				void EmplaceData(Entity entity, T&& component)
				{
					assert(entity_to_index_map.find(entity) == entity_to_index_map.end() && "Component added to same entity more than once.");
					// Put new entry at end
					size_t new_index = component_array.size();
					entity_to_index_map[entity] = new_index;
					index_to_entity_map[new_index] = entity;
					component_array.emplace_back(std::forward<T>(component));
				}

				Entity RemoveData(Entity entity)
				{
					assert(entity_to_index_map.find(entity) != entity_to_index_map.end() && "Removing non-existent component.");
					// Copy element at end into deleted element's place to maintain density
					size_t size = component_array.size();
					size_t index_of_removed_entity = entity_to_index_map[entity];
					size_t index_of_last_element = size - 1;
					component_array[index_of_removed_entity] = component_array[index_of_last_element];

					// Update map to point to moved spot
					Entity entity_of_last_element = index_to_entity_map[index_of_last_element];
					entity_to_index_map[entity_of_last_element] = index_of_removed_entity;
					index_to_entity_map[index_of_removed_entity] = entity_of_last_element;

					entity_to_index_map.erase(entity);
					index_to_entity_map.erase(index_of_last_element);
					component_array.pop_back();
					//return modified entity so
					//references to the component stored in the game can be updated
					return entity_of_last_element;
				}

				bool Contains(Entity entity) {
					return (entity_to_index_map.find(entity) != entity_to_index_map.end());
				}

				const T& GetConstData(Entity entity) const
				{
					auto it = entity_to_index_map.find(entity);
					if (it == entity_to_index_map.end()) {
						throw "Retrieving non-existent component.";
					}
					return component_array.at(it->second);
				}

				T& GetData(Entity entity)
				{
					auto it = entity_to_index_map.find(entity);
					if (it == entity_to_index_map.end()) {
						throw "Retrieving non-existent component.";
					}
					return component_array[it->second];
				}

				Entity EntityDestroyed(Entity entity) override
				{
					Entity e = ECS::INVALID_ENTITY_ID;
					if (entity_to_index_map.find(entity) != entity_to_index_map.end())
					{
						e = RemoveData(entity);
					}
					//Return modified entity due to removing data
					return e;
				}

				ComponentArray() {
					component_array.reserve(MAX_ENTITIES);
				}
			private:
				std::vector<T> component_array;
				std::unordered_map<Entity, size_t> entity_to_index_map;
				std::unordered_map<size_t, Entity> index_to_entity_map;
			};
		}
	}
}
