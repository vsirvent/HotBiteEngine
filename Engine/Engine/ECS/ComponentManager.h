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

#include "ComponentArray.h"
#include "Types.h"
#include <any>
#include <memory>
#include <unordered_map>
#include <set>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			class ComponentManager
			{
			public:
				template<typename T>
				void RegisterComponent()
				{
					const char* typeName = typeid(T).name();
					assert(component_types.find(typeName) == component_types.end() && "Registering component type more than once.");
					component_types.insert({ typeName, next_component_type });
					component_arrays.insert({ typeName, std::make_shared<ComponentArray<T>>() });
					++next_component_type;
				}

				template<typename T>
				ComponentType GetComponentType()
				{
					const char* typeName = typeid(T).name();
					assert(component_types.find(typeName) != component_types.end() && "Component not registered before use.");
					return component_types[typeName];
				}

				template<typename T>
				void AddComponent(Entity entity, const T& component)
				{
					GetComponentArray<T>()->InsertData(entity, component);
				}

				template<typename T>
				void EmplaceComponent(Entity entity, T&& component)
				{
					GetComponentArray<T>()->EmplaceData(entity, std::forward<T>(component));
				}

				template<typename T>
				Entity RemoveComponent(Entity entity)
				{
					return GetComponentArray<T>()->RemoveData(entity);
				}

				template<typename T>
				bool ContainsComponent(Entity entity)
				{
					return GetComponentArray<T>()->Contains(entity);
				}

				template<typename T>
				const T& GetConstComponent(Entity entity) const
				{
					return GetConstComponentArray<T>()->GetConstData(entity);
				}

				template<typename T>
				T& GetComponent(Entity entity)
				{
					return GetComponentArray<T>()->GetData(entity);
				}

				template<typename T>
				std::shared_ptr<ComponentArray<T>> GetComponents()
				{
					return GetComponentArray<T>();
				}

				std::set<Entity> EntityDestroyed(Entity entity)
				{
					std::set<Entity> modified_entities;
					for (auto const& pair : component_arrays)
					{
						auto const& component = pair.second;
						Entity e = component->EntityDestroyed(entity);
						if (e != ECS::INVALID_ENTITY_ID) {
							modified_entities.insert(e);
						}
					}
					return modified_entities;
				}

			private:
				std::unordered_map<const char*, ComponentType> component_types;
				std::unordered_map<const char*, std::shared_ptr<IComponentArray>> component_arrays;
				ComponentType next_component_type;

				template<typename T>
				const std::shared_ptr<ComponentArray<T>> GetConstComponentArray() const
				{
					const char* typeName = typeid(T).name();
					assert(component_types.find(typeName) != component_types.end() && "Component not registered before use.");
					return std::static_pointer_cast<ComponentArray<T>>(component_arrays.at(typeName));
				}

				template<typename T>
				::std::shared_ptr<ComponentArray<T>> GetComponentArray()
				{
					const char* typeName = typeid(T).name();
					assert(component_types.find(typeName) != component_types.end() && "Component not registered before use.");
					return std::static_pointer_cast<ComponentArray<T>>(component_arrays[typeName]);
				}
			};
		}
	}
}
