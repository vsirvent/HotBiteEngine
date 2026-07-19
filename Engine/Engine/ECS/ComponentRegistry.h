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

#include "Serialization.h"
#include "Coordinator.h"

#include <string>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace ECS {

			// How a component may be edited from a level file or the Scene Editor.
			//
			// Every policy is symmetric on purpose: a component the editor lets you
			// remove is always one it can put back. An asymmetric "you may delete this
			// but never recreate it" is a trap for whoever is authoring the scene, so
			// components that need data to exist (Mesh, Material) supply defaults from
			// World::GetDefaultMesh/GetDefaultMaterial instead of refusing to be added.
			enum class ComponentPolicy {
				// Part of what makes an entity an entity: never removed. Base carries
				// the name/id/visibility everything keys on; Transform is assumed by the
				// gizmo, by picking and by every render path. Still addable, to restore
				// the invariant on an entity somehow missing one.
				Mandatory,
				// Freely added and removed.
				Full,
				// Engine-managed: neither added nor removed by hand, because nothing a
				// user could type is what defines it (Camera is derived from its
				// Transform; Particles owns emitter data no JSON can rebuild).
				Locked
			};

			// The type-erased face of one serializable component type. Produced once per
			// type by ComponentRegistry::Register<T>() and driven by name thereafter, which
			// is what lets World::Load and the editor work over components they have no
			// compile-time knowledge of.
			struct ComponentDesc {
				std::string name;
				ComponentPolicy policy = ComponentPolicy::Full;

				bool (*has)(Coordinator*, Entity) = nullptr;
				// Add-or-update: creates the component if absent, then applies the JSON as
				// a delta over it. Notifies the signature change, which AddComponent alone
				// deliberately does not do.
				void (*apply)(const SerializeContext&, Entity, const nlohmann::json&) = nullptr;
				void (*remove)(const SerializeContext&, Entity) = nullptr;
				nlohmann::json(*serialize)(const SerializeContext&, Entity) = nullptr;

				bool Removable() const {
					return policy == ComponentPolicy::Full;
				}
				bool Addable() const {
					return policy == ComponentPolicy::Full || policy == ComponentPolicy::Mandatory;
				}
			};

			// Process-wide table of serializable component types, keyed by T::NAME.
			//
			// Global rather than per-World because the descriptors are stateless - every
			// entry point takes the SerializeContext that says *which* world and coordinator
			// to act on - so a host with several Worlds (the editor has a scene coordinator
			// and a templates coordinator) shares one table safely. Registration is
			// idempotent for the same reason.
			class ComponentRegistry {
			public:
				static ComponentRegistry& Instance() {
					static ComponentRegistry instance;
					return instance;
				}

				// Registering the same NAME twice keeps the first entry. Two different
				// types claiming one name is a bug in the game, but silently letting the
				// second win would be a far more confusing one, so the first registration
				// stands and the clash is reported.
				template<typename T> requires SerializableComponent<T>
				void Register(ComponentPolicy policy = ComponentPolicy::Full) {
					const std::string name = T::NAME;
					if (const ComponentDesc* existing = Find(name)) {
						if (existing->serialize != &SerializeThunk<T>) {
							printf("ComponentRegistry: duplicate component name '%s'; keeping the "
								"first registration.\n", name.c_str());
						}
						return;
					}
					ComponentDesc desc;
					desc.name = name;
					desc.policy = policy;
					//Captureless lambdas, so these convert to plain function pointers and
					//the descriptor stays trivially copyable.
					desc.has = [](Coordinator* c, Entity e) -> bool {
						return c->ContainsComponent<T>(e);
					};
					desc.apply = [](const SerializeContext& ctx, Entity e, const nlohmann::json& j) {
						//Components reach their siblings through ctx.entity, so it is
						//stamped here rather than trusted from the caller.
						SerializeContext local = ctx;
						local.entity = e;
						if (!local.coordinator->ContainsComponent<T>(e)) {
							local.coordinator->AddComponent<T>(e);
						}
						local.coordinator->GetComponent<T>(e).FromJson(j, local);
						local.coordinator->NotifySignatureChange(e);
					};
					desc.remove = [](const SerializeContext& ctx, Entity e) {
						if (ctx.coordinator->ContainsComponent<T>(e)) {
							//RemoveComponent notifies the signature change itself, for this
							//entity and for whichever entity got swapped into its slot.
							ctx.coordinator->RemoveComponent<T>(e);
						}
					};
					desc.serialize = &SerializeThunk<T>;
					descriptors.push_back(std::move(desc));
				}

				const ComponentDesc* Find(const std::string& name) const {
					for (const ComponentDesc& d : descriptors) {
						if (d.name == name) {
							return &d;
						}
					}
					return nullptr;
				}

				const std::vector<ComponentDesc>& All() const { return descriptors; }

			private:
				ComponentRegistry() = default;

				//Named rather than inline so Register can compare identity to tell a
				//re-registration of the same type from a genuine name clash.
				template<typename T>
				static nlohmann::json SerializeThunk(const SerializeContext& ctx, Entity e) {
					SerializeContext local = ctx;
					local.entity = e;
					return local.coordinator->GetConstComponent<T>(e).ToJson(local);
				}

				std::vector<ComponentDesc> descriptors;
			};
		}
	}
}
