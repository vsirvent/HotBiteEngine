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

#include <Defines.h>
#include <ECS/Serialization.h>

namespace HotBite {
	namespace Engine {
		namespace Components {

			// Platforms: entities the engine moves by itself, on a repeating path.
			//
			// Two components rather than one, because they answer two different
			// authoring questions and share no parameter:
			//
			//   Platform       - "oscillate about where you are, and/or spin": the lift
			//                    that rises and falls, the fan, the swaying bridge. The
			//                    motion is a sine, so it has an amplitude and a frequency
			//                    and never reaches an end.
			//   LinearPlatform - "travel from here to there at this speed": the patrolling
			//                    slab, the rising laser. The motion is constant-speed
			//                    between two ends, so it has a length and a speed and
			//                    reverses when it arrives.
			//
			// Everything they share is in the *system* (PlatformSystem.h): the delay, the
			// riders a solid platform carries with it, and the fact that both work with or
			// without a Physics component.
			//
			// Both express their rates per *second* and compute their pose absolutely from
			// their own clock, never by integrating a step per tick. That is what makes
			// them behave identically at any physics rate, and what makes a platform come
			// back to exactly its authored position every cycle: an integrated sine drifts,
			// and its amplitude ends up being whatever the tick rate happens to be.

			struct Platform {
				static constexpr const char* NAME = "Platform";

				// == Linear oscillation ==
				// The platform sits at `centre + linear_dir * amplitude * sin(...)`, where
				// the centre is the pose it was authored at. `linear_dir` need not be a
				// unit vector; it is normalized on use, so it reads as a direction rather
				// than as a second amplitude.
				float3 linear_dir{ 0.0f, 1.0f, 0.0f };
				//Peak displacement from the centre, in world units. 0 disables the
				//oscillation (and with it every cost of computing it).
				float amplitude = 0.0f;
				//Full cycles per second. 0.25 is one round trip every four seconds.
				float freq = 0.0f;
				//Where in the cycle the platform starts, in turns (0..1). A row of
				//otherwise identical platforms is desynchronized with this and nothing
				//else - which is the only reason it exists, since a level that staggers
				//its platforms by giving them different speeds ends up with platforms
				//that also move at different speeds.
				float phase = 0.0f;

				// == Angular spin ==
				// A continuous rotation about `angular_dir`, composed on top of the
				// rotation the platform was authored with - so that rotation is the spin's
				// zero rather than something the spin overwrites.
				float3 angular_dir{ 0.0f, 1.0f, 0.0f };
				//Radians per second. 0 disables the spin.
				float angular_speed = 0.0f;

				//Seconds the platform stays still before it starts moving. Measured from
				//when the platform first ticks, so it also re-arms on a restart (see
				//`Runtime` below).
				float delay = 0.0f;

				//Seconds after the platform starts moving at which it stops being driven
				//and its rigid body is turned DYNAMIC - the crumbling ledge that gives way
				//underfoot. Negative (the default) means never, and it does nothing at all
				//on a platform with no Physics component: there is no body to hand over to
				//the simulation.
				float fall_delay = -1.0f;

				// Live state. Not serialized (see the contract at the top of
				// ECS/Serialization.h) and not authored: PlatformSystem owns every field
				// here. It lives on the component rather than in the system's own record
				// because ECS::EntityVector::Insert *overwrites* an existing record, and a
				// signature change - which the Scene Editor raises on every component edit
				// - would otherwise re-latch `centre` at wherever the platform happened to
				// be mid-swing, walking the whole scene's platforms out of place one
				// inspector tweak at a time.
				struct Runtime {
					//Seconds this platform has been ticking, delay included.
					float clock = 0.0f;
					//The pose the motion is measured from, captured on the first tick. The
					//rotation is latched here rather than read from
					//Transform::initial_rotation, which only FBXLoader ever writes: a
					//platform rotated in a level file or with the editor's gizmo would
					//otherwise snap back to the rotation it was imported at the moment it
					//began to spin.
					float3 centre{};
					float4 base_rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
					bool latched = false;
					//What the system last wrote, so it can tell its own motion from someone
					//else having moved the platform (a gizmo drag, an undo, a
					//physics-preview rewind) and re-latch instead of drifting.
					float3 last_applied{};
					float4 last_applied_rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
					//Set once `fall_delay` has elapsed and the body has been handed over.
					bool fallen = false;
				} rt;

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			struct LinearPlatform {
				static constexpr char const* NAME = "LinearPlatform";

				//The far end of the path, as an offset from the authored position. Relative
				//rather than a pair of absolute endpoints on purpose: the path then follows
				//the entity, so moving a platform in the editor moves what it patrols
				//instead of leaving its route behind at the old place.
				float3 travel{ 0.0f, 1.0f, 0.0f };
				//World units per second along `travel`.
				float speed = 0.0f;
				//Seconds before the platform starts moving, exactly as Platform::delay.
				float delay = 0.0f;
				//Reverse at each end (the default) or stop on arrival at the far end.
				bool ping_pong = true;

				//See Platform::Runtime - same reasons, same ownership.
				struct Runtime {
					float clock = 0.0f;
					float3 start{};
					bool latched = false;
					//How far along `travel` the platform is, 0 at the start and 1 at the far
					//end. A normalized parameter rather than a position compared against the
					//endpoints: the platform then lands exactly on its ends however long a
					//tick is, and a step longer than what is left cannot carry it past them.
					float t = 0.0f;
					bool forward = true;
					float3 last_applied{};
				} rt;

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};
		}
	}
}
