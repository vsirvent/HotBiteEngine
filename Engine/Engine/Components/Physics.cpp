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

//Builds the body's collider for the given scale/rotation and attaches it. The
//caller holds physics_mutex and has already cleared any previous collider.
//Shared by Init and UpdateShape so a rebuilt collider is byte-for-byte the one
//the entity would have been born with at that scale.
void Physics::AddCollider(ShapeData* shape_data, const float3& extends,
	const float3& s, const float4& r) {
	Quaternion q{ r.x, r.y, r.z, r.w };
	CollisionShape* cshape = NULL;
	owned_shape = nullptr;
	owned_mesh = nullptr;
	if (shape_data == NULL) {
		float3 e = MULT_F3_F3(extends, s);
		vector4d xm_r = XMVectorSet(r.x, r.y, r.z, r.w);
		vector3d xm_e = XMVectorSet(e.x, e.y, e.z, 1.0f);
		XMStoreFloat3(&e, XMVector3Rotate(xm_e, xm_r));
		e.x = abs(e.x); e.y = abs(e.y); e.z = abs(e.z);
		float radius = (e.x + e.z) / 4.0f;
		switch (this->shape) {
		case eShapeForm::SHAPE_CAPSULE: {
			if (radius > 0.0f) {
				cshape = physics_common.createCapsuleShape(radius, e.y * 2.0f);
				Transform tshape(Vector3{ 0.0f, 0.0f,  radius + e.y }, q.getUnit());
				collider = body->addCollider(cshape, tshape);
			}
		}break;
		case eShapeForm::SHAPE_BOX: {
			cshape = physics_common.createBoxShape({ e.x, e.y, e.z });
			Transform tshape(Vector3{ 0.0f, 0.0f,  0.0f }, q.getUnit());
			collider = body->addCollider(cshape, tshape);
		}break;
		case eShapeForm::SHAPE_SPHERE: {
			if (radius > 0.0f) {
				cshape = physics_common.createSphereShape(e.y);
				Transform tshape(Vector3{ 0.0f, 0.0f, 0.0f }, q.getUnit());
				collider = body->addCollider(cshape, tshape);
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
	BodyType body_type, ShapeData* shape_data, const float3& extends,
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
		AddCollider(shape_data, extends, s, r);
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

bool Physics::UpdateShape(ShapeData* shape_data, const float3& extends,
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
	AddCollider(shape_data, extends, s, r);
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
