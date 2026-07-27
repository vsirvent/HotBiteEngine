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

#include "Physics.h"
#include <Core/PhysicsCommon.h>
#include <DirectXMath.h>

using namespace reactphysics3d;
using namespace DirectX;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;

std::unordered_map<reactphysics3d::RigidBody*, uint32_t> Physics::mBodyRefs;

//Owns everything a privately scaled mesh collider needs. The vertex/index arrays
//must outlive the shape: TriangleVertexArray only references caller memory.
struct Physics::ScaledMesh {
	std::vector<float3> vertices;
	std::vector<float3> normals;
	std::vector<unsigned int> indices;
	TriangleVertexArray* array = nullptr;
	TriangleMesh* mesh = nullptr;

	~ScaledMesh() {
		if (mesh != nullptr) {
			physics_common.destroyTriangleMesh(mesh);
		}
		delete array;
	}
};

//Which of the entity's local axes stands most nearly upright once the body's
//rotation is applied.
//
//A capsule has exactly one axis (reactphysics3d runs it along the shape's local Y)
//and the objects that use one are characters, for which that axis is the vertical
//one. Which *local* axis is vertical depends on how the model was authored - a Z-up
//FBX carries a -90 degree X rotation to stand it up - so it is read off the
//rotation. It is deliberately not guessed from which extent is longest: a rig's
//widest axis is its arm span as often as its height.
static int MostVerticalAxis(const float4& r) {
	const vector4d q = XMVectorSet(r.x, r.y, r.z, r.w);
	int axis = 1;
	float best = -1.0f;
	for (int i = 0; i < 3; ++i) {
		const vector3d local = XMVectorSet(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f,
			i == 2 ? 1.0f : 0.0f, 0.0f);
		const float up = fabsf(XMVectorGetY(XMVector3Rotate(local, q)));
		if (up > best) {
			best = up;
			axis = i;
		}
	}
	return axis;
}

//Builds the body's collider for the given scale/rotation and attaches it. The
//caller holds physics_mutex and has already cleared any previous collider.
//Shared by Init and UpdateShape so a rebuilt collider is byte-for-byte the one
//the entity would have been born with at that scale.
void Physics::AddCollider(ShapeData* shape_data, const box& local_box,
	const float3& s, const float4& r) {
	CollisionShape* cshape = NULL;
	owned_shape = nullptr;
	owned_mesh = nullptr;
	if (shape_data == NULL) {
		//Every primitive below is built in BODY space, which is the entity's local
		//space scaled: half extents and centre are the local box multiplied by the
		//entity scale, and the body's own transform - which already carries the
		//entity rotation - is what orients them. That rotation must NOT be applied
		//to the collider as well. Doing so (and rotating the extents *vector*, which
		//is not how a box rotates in the first place) is what left the troll's
		//collider tilted against its own bounding box, and sized by whichever axes
		//the rotation happened to mix.
		//The centre is what keeps the shape on the model: a mesh is rarely centred
		//on its entity origin, and dropping it sank half of the troll's box into the
		//ground. The mesh-collider branch below has always worked this way - its
		//triangles are mesh-space and its local transform is the identity - so this
		//is the primitives joining the convention rather than a new one.
		//Zero is a legal extent for an authored box (a plane) but not for a
		//collision shape, so a degenerate axis becomes a thin one instead.
		constexpr float MIN_EXTENT = 1e-3f;
		const float half[3] = {
			(std::max)(fabsf(local_box.Extents.x * s.x), MIN_EXTENT),
			(std::max)(fabsf(local_box.Extents.y * s.y), MIN_EXTENT),
			(std::max)(fabsf(local_box.Extents.z * s.z), MIN_EXTENT) };
		const Vector3 offset(local_box.Center.x * s.x, local_box.Center.y * s.y,
			local_box.Center.z * s.z);
		switch (this->shape) {
		case eShapeForm::SHAPE_CAPSULE: {
			const int axis = MostVerticalAxis(r);
			//Half of the two cross-section half extents: a circle cannot match a
			//rectangle, so the larger would give a character the width of its arm
			//span and the smaller would walk it through walls its shoulders do not
			//fit through.
			//A cross-section as wide as the axis is long holds no capsule at all, and
			//reactphysics3d asserts on a non-positive height, so there the radius is
			//what gives way: the shape degenerates towards the sphere such a box
			//should get.
			const float radius = (std::min)((half[(axis + 1) % 3] + half[(axis + 2) % 3]) * 0.5f,
				half[axis] * 0.99f);
			//`height` is the distance between the two cap CENTRES, so the capsule is
			//height + 2*radius long: shorten it by the caps and it ends exactly where
			//the box does. Passing the full extent as the height is what made the old
			//capsule a radius taller than the model at each end, standing the demo
			//troll on air.
			const float height = 2.0f * (half[axis] - radius);
			if (radius > 0.0f) {
				cshape = physics_common.createCapsuleShape(radius, height);
				//Shape local Y -> the chosen local axis.
				Quaternion align = Quaternion::identity();
				if (axis == 0) {
					align = Quaternion::fromEulerAngles(0.0f, 0.0f, -PI_RP3D / 2.0f);
				}
				else if (axis == 2) {
					align = Quaternion::fromEulerAngles(PI_RP3D / 2.0f, 0.0f, 0.0f);
				}
				collider = body->addCollider(cshape, Transform(offset, align));
			}
		}break;
		case eShapeForm::SHAPE_BOX: {
			cshape = physics_common.createBoxShape({ half[0], half[1], half[2] });
			collider = body->addCollider(cshape, Transform(offset, Quaternion::identity()));
		}break;
		case eShapeForm::SHAPE_SPHERE: {
			//As far as the box reaches along its longest axis: a sphere standing for a
			//box-shaped model is an approximation either way, and one that fits inside
			//the model lets it sink into the floor.
			const float radius = (std::max)((std::max)(half[0], half[1]), half[2]);
			if (radius > 0.0f) {
				cshape = physics_common.createSphereShape(radius);
				collider = body->addCollider(cshape, Transform(offset, Quaternion::identity()));
			}
		}break;
		}
	}
	else {
		//A mesh shape already carries a scale baked into its vertices, and is shared
		//by every entity and clone built from that mesh, so at that scale it is used
		//as-is and never touched. Anything else - an entity scaled in the editor, or
		//a clone whose transform differs from the entity the shape was authored from -
		//needs geometry of its own: a private ConcaveMeshShape over the same triangle
		//mesh, scaled by the ratio between the two.
		//Note the baseline comes from the SHAPE, not from this component: a clone
		//shares its source's shape while carrying its own, unrelated scale.
		//A zero baseline has no ratio to speak of; fall back to "unchanged" on that
		//axis rather than dividing.
		auto ratio = [](float now, float base) { return (base != 0.0f) ? now / base : 1.0f; };
		const float3& base_scale = shape_data->authored_scale;
		float3 relative{ ratio(s.x, base_scale.x), ratio(s.y, base_scale.y),
			ratio(s.z, base_scale.z) };
		bool unchanged = (relative.x == 1.0f && relative.y == 1.0f && relative.z == 1.0f);
		bool can_scale = !shape_data->vertices.empty() && shape_data->indices.size() >= 3;
		if (!unchanged && can_scale) {
			//Bake the scale into a private copy of the triangles and keep the shape
			//itself at scale 1 - see ScaledMesh in the header for why the library's
			//own scaling argument cannot be used here.
			owned_mesh = new ScaledMesh();
			owned_mesh->vertices.reserve(shape_data->vertices.size());
			for (const float3& v : shape_data->vertices) {
				owned_mesh->vertices.push_back({ v.x * relative.x, v.y * relative.y, v.z * relative.z });
			}
			owned_mesh->normals = shape_data->normals;
			owned_mesh->indices = shape_data->indices;
			//Constructed exactly like the shared shape in FBXLoader::LoadShapes
			//(strides included), so a scaled collider differs from an unscaled one
			//in nothing but the scale.
			owned_mesh->array = new TriangleVertexArray(
				(uint32_t)owned_mesh->vertices.size(),
				owned_mesh->vertices.data(), (uint32_t)sizeof(float3),
				owned_mesh->normals.data(), (uint32_t)sizeof(float),
				(uint32_t)owned_mesh->indices.size() / 3,
				owned_mesh->indices.data(), (uint32_t)sizeof(unsigned int) * 3,
				TriangleVertexArray::VertexDataType::VERTEX_FLOAT_TYPE,
				TriangleVertexArray::NormalDataType::NORMAL_FLOAT_TYPE,
				TriangleVertexArray::IndexDataType::INDEX_INTEGER_TYPE);
			owned_mesh->mesh = physics_common.createTriangleMesh();
			owned_mesh->mesh->addSubpart(owned_mesh->array);
			cshape = physics_common.createConcaveMeshShape(owned_mesh->mesh);
			owned_shape = cshape;
		}
		else {
			cshape = shape_data->shape;
		}
		collider = body->addCollider(cshape, Transform::identity());
	}
	if (shape_data == NULL) {
		//Every primitive shape above was created here and now, so it belongs to
		//this component (a ShapeData shape is shared and must never be destroyed).
		owned_shape = cshape;
	}
	if (collider != nullptr) {
		Material& material = collider->getMaterial();
		material.setBounciness(0.01f);
		material.setFrictionCoefficient(0.5f);
	}
}

bool Physics::Init(PhysicsWorld* w,
	BodyType body_type, ShapeData* shape_data, const box& local_box,
	const float3& p, const float3& s, const float4& r, eShapeForm form) {
	physics_mutex.lock();
	world = w;
	Quaternion q{ r.x, r.y, r.z, r.w };
	Transform t(Vector3{ p.x, p.y, p.z }, q.getUnit());
	body = w->createRigidBody(t);
	this->shape = form;
	this->type = body_type;
	if (body != nullptr) {
		body->setType(body_type);
		mBodyRefs[body]++;
		AddCollider(shape_data, local_box, s, r);
	}
	if (bounce >= 0.0f) {
		GetMaterial()->setBounciness(bounce);
	}
	if (friction >= 0.0f) {
		GetMaterial()->setFrictionCoefficient(friction);
	}	
	if (air_friction >= 0.0f) {
		body->setLinearDamping(friction);
	}	
	physics_mutex.unlock();
	return (body != nullptr);
}

bool Physics::UpdateShape(ShapeData* shape_data, const box& local_box,
	const float3& s, const float4& r) {
	std::lock_guard<std::recursive_mutex> lock(physics_mutex);
	if (body == nullptr) {
		return false;
	}
	//Bounciness/friction live on the collider's material and are about to be
	//thrown away with it; carry the current values over to the replacement so a
	//rescale never silently resets them.
	float current_bounce = bounce;
	float current_friction = friction;
	CollisionShape* previous_owned = owned_shape;
	ScaledMesh* previous_mesh = owned_mesh;
	if (collider != nullptr) {
		Material& material = collider->getMaterial();
		current_bounce = material.getBounciness();
		current_friction = material.getFrictionCoefficient();
		body->removeCollider(collider);
		collider = nullptr;
	}
	AddCollider(shape_data, local_box, s, r);
	//Only now that nothing references it: a scale drag calls this every frame, so
	//leaving the replaced shapes around would grow without bound.
	if (previous_owned != nullptr && previous_owned != owned_shape) {
		switch (previous_owned->getName()) {
		case CollisionShapeName::CAPSULE:
			physics_common.destroyCapsuleShape((CapsuleShape*)previous_owned);
			break;
		case CollisionShapeName::BOX:
			physics_common.destroyBoxShape((BoxShape*)previous_owned);
			break;
		case CollisionShapeName::SPHERE:
			physics_common.destroySphereShape((SphereShape*)previous_owned);
			break;
		case CollisionShapeName::TRIANGLE_MESH:
			physics_common.destroyConcaveMeshShape((ConcaveMeshShape*)previous_owned);
			break;
		default:
			break;
		}
	}
	//The geometry the destroyed shape referenced, freed only after it (the shape
	//holds a BVH over these very arrays).
	if (previous_mesh != nullptr && previous_mesh != owned_mesh) {
		delete previous_mesh;
	}
	if (collider != nullptr) {
		Material& material = collider->getMaterial();
		if (current_bounce >= 0.0f) {
			material.setBounciness(current_bounce);
		}
		if (current_friction >= 0.0f) {
			material.setFrictionCoefficient(current_friction);
		}
	}
	return true;
}

Physics::~Physics() {
	physics_mutex.lock();
	if (body != nullptr) {
		auto it = mBodyRefs.find(body);
		if (it != mBodyRefs.end()) {
			if (--(it->second) == 0) {
				world->destroyRigidBody(it->first);
				mBodyRefs.erase(it);
			}
		}
		body = nullptr;
	}
	physics_mutex.unlock();
}

Physics::Physics(const Physics& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	if (body != nullptr) {
		mBodyRefs[body]++;
	}
	physics_mutex.unlock();
}

Physics::Physics(Physics&& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	other.body = nullptr;
	physics_mutex.unlock();
}

Physics& Physics::operator=(const Physics& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	if (body != nullptr) {
		mBodyRefs[body]++;
	}
	physics_mutex.unlock();
	return *this;
}

Physics& Physics::operator=(Physics&& other) {
	physics_mutex.lock();
	Physics::~Physics();
	memcpy(this, &other, sizeof(Physics));
	other.body = nullptr;
	physics_mutex.unlock();
	return *this;
}

void
Physics::SetEnabled(bool enabled) {
	if (body != nullptr) {
		body->setIsActive(enabled);
	}
}

//Qualified: a return type is resolved before the Physics:: scope applies.
const std::vector<HotBite::Engine::float3>* Physics::GetOwnedMeshVertices() const {
	return (owned_mesh != nullptr) ? &owned_mesh->vertices : nullptr;
}

const std::vector<unsigned int>* Physics::GetOwnedMeshIndices() const {
	return (owned_mesh != nullptr) ? &owned_mesh->indices : nullptr;
}

reactphysics3d::Material* Physics::GetMaterial() {
	reactphysics3d::Material* m = nullptr;
	if (collider != nullptr) {
		m = &(collider->getMaterial());
	}
	return m;
}
