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

// Physics::ToJson / Physics::FromJson.
//
// Split out of Physics.cpp rather than living beside the rest of the component
// because deserializing needs the sibling Bounds/Transform components, and
// including Components/Base.h into Physics.cpp makes HotBite's Components::Material
// and Components::Transform visible unqualified alongside reactphysics3d's
// identically named types - which that file uses unqualified throughout, under
// `using namespace reactphysics3d`. Keeping the two namespaces apart in separate
// translation units is cheaper than qualifying every use in the collider code.

#include "Physics.h"
#include "Base.h"
#include <World.h>

using nlohmann::json;
using HotBite::Engine::ECS::SerializeContext;

namespace HotBite {
	namespace Engine {
		namespace Components {

			static const char* BodyTypeToStr(reactphysics3d::BodyType t) {
				switch (t) {
				case reactphysics3d::BodyType::DYNAMIC:   return "DYNAMIC";
				case reactphysics3d::BodyType::KINEMATIC: return "KINEMATIC";
				default:                                  return "STATIC";
				}
			}

			static const char* ShapeToStr(Physics::eShapeForm s) {
				switch (s) {
				case Physics::SHAPE_CAPSULE: return "CAPSULE";
				case Physics::SHAPE_BOX:     return "BOX";
				case Physics::SHAPE_SPHERE:  return "SPHERE";
				default:                     return "NONE";
				}
			}

			json Physics::ToJson(const SerializeContext& ctx) const {
				json j;
				j["type"] = BodyTypeToStr(type);
				j["shape"] = ShapeToStr(shape);
				//Negative means "engine default"; only the overrides a level actually set
				//are written, so a round-trip does not bake -1 into every entity.
				if (bounce >= 0.0f) {
					j["bounce"] = bounce;
				}
				if (friction >= 0.0f) {
					j["friction"] = friction;
				}
				if (air_friction >= 0.0f) {
					j["air_friction"] = air_friction;
				}
				return j;
			}

			void Physics::FromJson(const json& j, const SerializeContext& ctx) {
				//Kept to tell an edit of the body's *form* from an edit of its material:
				//the first has to be rebuilt in the physics world, the second is a
				//setter on a collider that is already there.
				const reactphysics3d::BodyType previous_type = type;
				const eShapeForm previous_shape = shape;
				if (j.contains("type") && j["type"].is_string()) {
					const std::string t = j["type"];
					if (t == "DYNAMIC") {
						type = reactphysics3d::BodyType::DYNAMIC;
					}
					else if (t == "KINEMATIC") {
						type = reactphysics3d::BodyType::KINEMATIC;
					}
					else if (t == "STATIC") {
						type = reactphysics3d::BodyType::STATIC;
					}
					else {
						printf("Physics::FromJson: unknown body type '%s', keeping current.\n", t.c_str());
					}
				}
				if (j.contains("shape") && j["shape"].is_string()) {
					const std::string s = j["shape"];
					if (s == "CAPSULE") {
						shape = SHAPE_CAPSULE;
					}
					else if (s == "BOX") {
						shape = SHAPE_BOX;
					}
					else if (s == "SPHERE") {
						shape = SHAPE_SPHERE;
					}
					else if (s == "NONE") {
						shape = SHAPE_NONE;
					}
					else {
						printf("Physics::FromJson: unknown shape '%s', keeping current.\n", s.c_str());
					}
				}
				bounce = j.value("bounce", bounce);
				friction = j.value("friction", friction);
				air_friction = j.value("air_friction", air_friction);

				if (body != nullptr) {
					//Already in the physics world: push the changes at it rather than
					//leaving the component describing a body that does not match.
					//
					//Body type and shape are the two that need more than a setter: the
					//collision shape was built from the Transform (and from the body type,
					//which decides whether a mesh collider is allowed at all) when the body
					//was created, so both are rebuilt through UpdateShape. Without this an
					//editor could change either and see nothing happen - the values would
					//round-trip through the file and only take effect on the next load.
					if (type != previous_type) {
						std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
						body->setType(type);
						if (type == reactphysics3d::BodyType::DYNAMIC) {
							//Whatever a kinematic/static body was carrying is not a velocity
							//the user asked for.
							body->setLinearVelocity({ 0.0f, 0.0f, 0.0f });
							body->setAngularVelocity({ 0.0f, 0.0f, 0.0f });
						}
					}
					if ((type != previous_type || shape != previous_shape) &&
						ctx.coordinator != nullptr && ctx.entity != ECS::INVALID_ENTITY_ID &&
						ctx.coordinator->ContainsComponent<Bounds>(ctx.entity) &&
						ctx.coordinator->ContainsComponent<Transform>(ctx.entity)) {
						const Bounds& bounds = ctx.coordinator->GetConstComponent<Bounds>(ctx.entity);
						const Transform& transform =
							ctx.coordinator->GetConstComponent<Transform>(ctx.entity);
						//Same rule the create path below follows: a static body collides
						//against its own mesh when it has one, everything else uses the
						//primitive form.
						Core::ShapeData* shape_data = nullptr;
						if (type == reactphysics3d::BodyType::STATIC && ctx.world != nullptr &&
							ctx.coordinator->ContainsComponent<Base>(ctx.entity)) {
							shape_data = ctx.world->GetEntityShape(
								ctx.coordinator->GetConstComponent<Base>(ctx.entity).name);
						}
						UpdateShape(shape_data, bounds.local_box.Extents, transform.scale,
							transform.rotation);
					}
					//Values below zero mean "leave the engine default alone".
					if (collider != nullptr) {
						if (bounce >= 0.0f) {
							collider->getMaterial().setBounciness(bounce);
						}
						if (friction >= 0.0f) {
							collider->getMaterial().setFrictionCoefficient(friction);
						}
					}
					if (air_friction >= 0.0f) {
						body->setLinearDamping(air_friction);
					}
					return;
				}

				//No body yet: build one, the way SpawnInstance and World::Init do. This is
				//what makes a Physics component added from a level file or from the editor
				//actually participate in the simulation instead of being inert data.
				if (ctx.world == nullptr || ctx.coordinator == nullptr ||
					ctx.entity == ECS::INVALID_ENTITY_ID) {
					return;
				}
				if (!ctx.coordinator->ContainsComponent<Bounds>(ctx.entity) ||
					!ctx.coordinator->ContainsComponent<Transform>(ctx.entity)) {
					printf("Physics::FromJson: entity has no Bounds/Transform, cannot create a body.\n");
					return;
				}
				const Bounds& bounds = ctx.coordinator->GetConstComponent<Bounds>(ctx.entity);
				const Transform& transform = ctx.coordinator->GetConstComponent<Transform>(ctx.entity);

				//Static bodies collide against the entity's own FBX mesh when it has one;
				//dynamic and kinematic bodies always use the primitive form named above.
				//Looking the shape up by entity name is what World::Init does, and it
				//resolves clones too.
				Core::ShapeData* shape_data = nullptr;
				if (type == reactphysics3d::BodyType::STATIC &&
					ctx.coordinator->ContainsComponent<Base>(ctx.entity)) {
					shape_data = ctx.world->GetEntityShape(
						ctx.coordinator->GetConstComponent<Base>(ctx.entity).name);
				}
				//The local box, not bounding_box: Init applies transform.scale itself.
				Init(ctx.world->GetPhysicsWorld(), type, shape_data, bounds.local_box.Extents,
					transform.position, transform.scale, transform.rotation, shape);
			}
		}
	}
}
