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

			// A force field: an entity that pushes every dynamic body it reaches, in one
			// direction. The fan that lifts you up a shaft, the jump pad, the conveyor,
			// the wind blowing along a corridor.
			//
			// It is not a collider and not a trigger. The two shapes below are evaluated
			// by ForceSystem against each dynamic body's position, so a force field needs
			// no collision geometry of its own except in the TOUCH case, where the
			// question it asks *is* "is something touching me".
			struct Force {
				static constexpr const char* NAME = "Force";

				// What decides whether a body is in the field.
				enum eType {
					//Nothing. The default, so adding the component does not immediately
					//start shoving the level around.
					NONE,
					//Anything in contact with this entity's collider, at full strength.
					//Needs a Physics component, and the collider is usually a trigger
					//(reactphysics3d::Collider::setIsTrigger) so the pad does not also
					//bounce what it is meant to push.
					TOUCH,
					//A cylinder of `radius` reaching `range` from the entity's position
					//along `dir`: the beam of a fan. Needs no collider at all.
					PROJECTION,
				};

				eType type = NONE;
				//Which way the field pushes. Not necessarily the axis of the PROJECTION
				//cylinder's own orientation - it is both, which is the point: a fan blows
				//along the shaft it reaches down.
				float3 dir{ 0.0f, 1.0f, 0.0f };
				//False (the default) reads `dir` as a world direction, which is what a
				//level that authors "up" or "along +Z" per entity wants and what keeps
				//existing content unchanged. True turns it by the entity's rotation, so a
				//rotated jump pad pushes where it points and one force template can be
				//placed at any angle.
				bool local_dir = false;

				//Magnitude in newtons, applied at the body's centre of mass. This is the
				//value reached at `range` - see `origin_force`.
				float force = 0.0f;
				//Magnitude at the entity itself, ramping linearly to `force` at `range`.
				//Zero (the default) is the profile a fan wants: weakest at the nozzle, so a
				//body does not sit pinned against it, and strongest where the beam ends, so
				//what the fan lifts arrives at the far end rather than hovering halfway.
				//Set it equal to `force` for a uniform field, or above it for a thruster
				//that fades with distance. Ignored by TOUCH, which has no distance.
				float origin_force = 0.0f;

				// == PROJECTION only ==
				//How far the beam reaches, in world units, measured from the entity's
				//position. A body further away than this is outside the field whatever
				//`radius` says.
				float range = 0.0f;
				//Half-width of the beam: a body is in the field when it is within this
				//distance of the beam's axis.
				float radius = 0.0f;

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);

				//`type` round-trips as a name rather than as the integer the enum happens
				//to be, so a level file says what it means and a new class can be inserted
				//without renumbering every existing one. Shared with the Scene Editor's
				//combo, which must not carry a second copy of the spelling.
				static const char* TypeName(eType t);
				static eType TypeFromName(const std::string& name, eType fallback);
			};
		}
	}
}
