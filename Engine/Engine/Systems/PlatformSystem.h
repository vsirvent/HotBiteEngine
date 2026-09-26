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

//Physics.h first, and that is load-bearing: it opens with reactphysics3d's own headers,
//which have to be seen before Defines.h pulls in <winsock2.h> and with it the Windows
//min/max *macros*. Those macros mangle reactphysics3d's Vector2::min/max declarations
//into a hundred syntax errors inside a library header, which reads as the library being
//broken rather than as an include order. Every engine file that touches both does this.
#include <Components/Physics.h>
#include <Components/Base.h>
#include <Components/Platform.h>

#include <ECS/Coordinator.h>
#include <ECS/EntityVector.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {

			// The two platform systems (Components/Platform.h says what the two components
			// are for). They live in one pair of files because the interesting half is
			// shared: how a platform is *moved*, which is three separate concerns neither
			// component expresses on its own.
			//
			// 1. WITH OR WITHOUT A RIGID BODY. A platform carrying a Physics component is
			//    moved by writing its body's transform, and PhysicsSystem::Update then
			//    copies the body's pose back into the Transform. One without is moved by
			//    writing the Transform directly and marking it dirty, which is
			//    StaticMeshSystem's cue to recompose the world matrix. Same component,
			//    same authoring, and the difference is a pointer being null - so a solid
			//    lift and a piece of moving scenery are the same thing to a level author.
			//
			//    A STATIC body is promoted to KINEMATIC the first time the platform actually
			//    moves - not when it is first seen, since one sitting at its defaults is
			//    inert and neither its body type nor its shadow path should change for
			//    that - and `Base::is_static` is cleared with it. Both are silent failures,
			//    and neither looks like what it is: PhysicsSystem::Update writes a
			//    Transform back only for a body that is not STATIC, so a STATIC platform
			//    slides through the collision world while standing still on screen; and a
			//    static caster is drawn only into a directional light's static shadow map,
			//    so a platform left marked static drags a shadow that stays where the
			//    platform was authored.
			//
			// 2. RIDERS. What stands on a solid platform has to travel with it.
			//    reactphysics3d moves nothing on its own here - a KINEMATIC body imparts no
			//    friction - so each of the platform's DYNAMIC contacts is displaced by the
			//    same delta the platform moved. Only DYNAMIC ones: a kinematic or static
			//    body in contact belongs to something else (another platform, the level),
			//    and shoving it would have two owners writing one pose.
			//
			//    A spin carries its riders by *orbiting* them - rotating each rider's
			//    position about the platform's own axis - and deliberately leaves their
			//    orientation alone. Copying the platform's orientation onto a rider, which
			//    is the obvious reading of "carry it", spins the rider in place while
			//    leaving it parked at one spot on a turning carousel.
			//
			// 3. RESTARTING. Both components measure their motion from the pose they were
			//    authored at, captured on the first tick - the position *and*, for a spin,
			//    the rotation, which is deliberately not Transform::initial_rotation (only
			//    FBXLoader ever writes that one, so a platform rotated in a level file or
			//    with the editor's gizmo would snap back to its import rotation the moment
			//    it began to turn). The systems also notice when a platform is no longer
			//    where they last put it - a gizmo drag, an undo, the physics preview's
			//    rewind - and re-latch there rather than swinging about a centre the
			//    platform has left. Without that, authoring a moving platform in the editor
			//    walks it a little further off with every edit.
			//
			// Both run on the physics thread, under Core::physics_mutex, from World::Run -
			// and only while physics is not paused, so a platform sits at its authored pose
			// in the Scene Editor until Edit/Simulate Physics is switched on. Their clocks
			// accumulate only on the ticks they actually run, so a pause does not fast
			// forward a platform to wherever it would have been.

			class PlatformSystem : public ECS::System {
			private:
				struct PlatformEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::Platform* platform = nullptr;
					//Null for a platform with no rigid body, which is a supported and
					//ordinary case rather than an error - see the note above.
					Components::Physics* physics = nullptr;

					PlatformEntity(ECS::Coordinator* c, ECS::Entity entity);
				};

				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature signature;
				ECS::EntityVector<PlatformEntity> platforms;

				void Update(PlatformEntity& entity, float elapsed_seconds);

			public:
				PlatformSystem() = default;
				virtual ~PlatformSystem() {}

				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

				void Update(int64_t elapsed_nsec, int64_t total_nsec);

				//How many platforms are being driven. The Scene Editor's automation channel
				//reports this: a platform that is not moving and a platform this system has
				//never seen look identical in a screenshot.
				size_t Count() const { return platforms.GetConstData().size(); }
			};

			class LinearPlatformSystem : public ECS::System {
			private:
				struct LinearPlatformEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::LinearPlatform* platform = nullptr;
					Components::Physics* physics = nullptr;

					LinearPlatformEntity(ECS::Coordinator* c, ECS::Entity entity);
				};

				ECS::Coordinator* coordinator = nullptr;
				ECS::Signature signature;
				ECS::EntityVector<LinearPlatformEntity> platforms;

				void Update(LinearPlatformEntity& entity, float elapsed_seconds);

			public:
				LinearPlatformSystem() = default;
				virtual ~LinearPlatformSystem() {}

				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

				void Update(int64_t elapsed_nsec, int64_t total_nsec);

				size_t Count() const { return platforms.GetConstData().size(); }
			};
		}
	}
}
