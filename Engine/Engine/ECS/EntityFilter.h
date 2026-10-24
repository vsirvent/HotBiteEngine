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

#include "Coordinator.h"
#include "Types.h"

namespace HotBite {
	namespace Engine {
		namespace ECS {
			// Which entities a system is about: those that carry every component of `include` and none of `exclude`.
			// A system builds one in OnRegister and asks Matches(signature) in OnEntitySignatureChanged:
			//
			//   filter.Include<Mover>(c).Include<Health>(c).Exclude<Downed>(c);
			//   if (filter.Matches(signature)) { entities.Insert(entity, ...); } else { entities.Remove(entity); }
			struct EntityFilter {
				Signature include;
				Signature exclude;

				template <class T> EntityFilter& Include(Coordinator* c) {
					include.set(c->GetComponentType<T>(), true);
					return *this;
				}
				template <class T> EntityFilter& Exclude(Coordinator* c) {
					exclude.set(c->GetComponentType<T>(), true);
					return *this;
				}
				bool Matches(const Signature& signature) const {
					return (signature & include) == include && (signature & exclude).none();
				}
				void Clear() {
					include.reset();
					exclude.reset();
				}
			};
		}
	}
}
