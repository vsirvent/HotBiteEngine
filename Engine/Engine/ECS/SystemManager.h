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

#include "System.h"
#include "Types.h"
#include <cassert>
#include <memory>
#include <unordered_map>

namespace HotBite {
	namespace Engine {
		namespace ECS {
			class SystemManager
			{
			public:
				template<typename T>
				std::shared_ptr<T> RegisterSystem()
				{
					const char* typeName = typeid(T).name();
					assert(systems.find(typeName) == systems.end() && "Registering system more than once.");
					std::shared_ptr<T> system = std::make_shared<T>();
					systems.insert({ typeName, system });
					return system;
				}

				template<typename T>
				std::shared_ptr<T> GetSystem()
				{
					const char* typeName = typeid(T).name();
					auto it = systems.find(typeName);
					assert(it != systems.end() && "System not found.");
					return std::dynamic_pointer_cast<T>(it->second);
				}

				void EntityDestroyed(Entity entity)
				{
					for (auto const& pair : systems)
					{
						pair.second->OnEntityDestroyed(entity);
					}
				}

				void EntitySignatureChanged(Entity entity, Signature entitySignature)
				{
					for (auto const& pair : systems)
					{
						pair.second->OnEntitySignatureChanged(entity, entitySignature);
					}
				}

			private:
				std::unordered_map<const char*, std::shared_ptr<System>> systems;
			};
		}
	}
}