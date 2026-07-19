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

// nlohmann's single-header json calls std::snprintf and std::isnan without
// including <cstdio>/<cmath> itself. It has always been pulled in late enough for
// something else to have done that first; this header is reached from component
// headers, which is early, so it does that explicitly rather than rely on order.
#include <cstdio>
#include <cmath>

#include <Core/Json.h>
#include <Defines.h>
#include <ECS/Types.h>
#include <concepts>
#include <string>

// Component serialization contract.
//
// A component becomes level-authorable - loadable from the "components" block of
// a level JSON, editable in the Scene Editor, and savable again - purely by
// declaring three members:
//
//     struct MyComponent {
//         static constexpr const char* NAME = "MyComponent";
//         nlohmann::json ToJson(const ECS::SerializeContext&) const;
//         void FromJson(const nlohmann::json&, const ECS::SerializeContext&);
//     };
//
// Nothing else is needed and nothing central has to be edited: World::RegisterComponent<T>
// detects the members through the SerializableComponent concept below and registers a
// descriptor automatically, so a *game's* own components (Marbles' Ball/Star/Trigger,
// DemoGame's CreatureComponent, ...) are first-class alongside the engine's. Components
// that do not declare them keep working exactly as before, just not from JSON.
//
// Two rules for implementers:
//
// 1. ToJson writes AUTHORING state only. Live runtime state - accumulated timers, cached
//    matrices, physics body pointers, "already triggered" flags - must not be written, or
//    saving a level freezes a mid-play snapshot into it. When in doubt, ask whether a level
//    designer would want to type the value; if not, leave it out.
//
// 2. FromJson must tolerate a partial object. Every field is optional and absent keys must
//    leave the current value untouched (use json::value(key, current)), because these blocks
//    are applied as *deltas* over whatever the FBX/template already produced. Never assume a
//    key is present.

namespace HotBite {
	namespace Engine {

		class World;

		namespace ECS {

			class Coordinator;

			// What a component needs to resolve things it cannot own itself: named assets
			// (a Material serializes as "floor", not as a pointer) and engine services (a
			// Physics body needs the physics world to be created in). Handed to every
			// ToJson/FromJson call.
			//
			// `world` may be null when a component is serialized outside a loaded world
			// (the Scene Editor inspecting a clipboard payload); components that need it
			// for asset lookup must degrade gracefully rather than dereference blindly.
			struct SerializeContext {
				World* world = nullptr;
				Coordinator* coordinator = nullptr;
				// The entity being (de)serialized. A component does not know its own
				// owner, but some need it: Physics builds its rigid body from the
				// sibling Bounds and Transform, which it can only reach through here.
				// Always filled in by the registry before it calls a component.
				Entity entity = INVALID_ENTITY_ID;
			};

			template<typename T>
			concept SerializableComponent =
				requires(T & t, const T & ct, const nlohmann::json & j, const SerializeContext & ctx) {
					{ T::NAME } -> std::convertible_to<const char*>;
					{ ct.ToJson(ctx) } -> std::same_as<nlohmann::json>;
					{ t.FromJson(j, ctx) };
			};

			// float3/float4 as {"x":..,"y":..,"z":..[,"w":..]}, matching the shape the
			// level format has always used for positions and rotations. Colors use the
			// same object form rather than the legacy "070070070" packed string: it is
			// lossless above 0.999 and readable in a diff.
			namespace JsonUtil {

				inline nlohmann::json FromFloat3(const float3& v) {
					return nlohmann::json{ {"x", v.x}, {"y", v.y}, {"z", v.z} };
				}

				inline nlohmann::json FromFloat4(const float4& v) {
					return nlohmann::json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
				}

				// Reads a float3/float4 only if `key` is present and an object, so a
				// missing key leaves `out` at its current value (delta semantics).
				inline void ToFloat3(const nlohmann::json& j, const char* key, float3& out) {
					if (!j.contains(key) || !j[key].is_object()) {
						return;
					}
					const nlohmann::json& v = j[key];
					out.x = v.value("x", out.x);
					out.y = v.value("y", out.y);
					out.z = v.value("z", out.z);
				}

				inline void ToFloat4(const nlohmann::json& j, const char* key, float4& out) {
					if (!j.contains(key) || !j[key].is_object()) {
						return;
					}
					const nlohmann::json& v = j[key];
					out.x = v.value("x", out.x);
					out.y = v.value("y", out.y);
					out.z = v.value("z", out.z);
					out.w = v.value("w", out.w);
				}
			}
		}
	}
}
