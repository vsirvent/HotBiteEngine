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

#include <Components\Physics.h>
#include "ParticleSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace DirectX;

void ParticleSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	particles_signature.set(coordinator->GetComponentType<Base>(), true);
	particles_signature.set(coordinator->GetComponentType<Transform>(), true);
	particles_signature.set(coordinator->GetComponentType<Particles>(), true);
	//The same two as SplatCloudSystem: an entity with either has its world matrix written by that system.
	mesh_signature.set(coordinator->GetComponentType<Mesh>(), true);
	mesh_signature.set(coordinator->GetComponentType<Bounds>(), true);
	physics_signature.set(coordinator->GetComponentType<Physics>(), true);
}


void ParticleSystem::OnEntityDestroyed(ECS::Entity entity) {
	particles.Remove(entity);
}

void ParticleSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {

	if ((entity_signature & particles_signature) == particles_signature)
	{
		ParticleEntity particle{ coordinator, entity };
		particle.owns_matrix = (entity_signature & mesh_signature) != mesh_signature && (entity_signature & physics_signature) != physics_signature;
		particles.Insert(entity, particle);
	}
	else
	{
		particles.Remove(entity);
	}
}

void ParticleSystem::ComposeWorldMatrix(ParticleEntity& entity) {
	Transform* transform = entity.transform;
	Base* base = entity.base;
	matrix trans = XMMatrixTranslation(transform->position.x, transform->position.y, transform->position.z);
	matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform->rotation));
	matrix scle = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z);
	transform->world_xmmatrix = scle * rot * trans;
	//A parent id can outlive the parent's components (destroying a composed object takes the root and its parts one at a time).
	if (base->parent != ECS::INVALID_ENTITY_ID && coordinator->ContainsComponent<Transform>(base->parent)) {
		const Transform& pt = coordinator->GetComponent<Transform>(base->parent);
		if (base->parent_rotation) {
			transform->world_xmmatrix = transform->world_xmmatrix * XMMatrixRotationQuaternion({ pt.rotation.x, pt.rotation.y, pt.rotation.z, pt.rotation.w });
		}
		if (base->parent_position) {
			transform->world_xmmatrix = transform->world_xmmatrix * XMMatrixTranslation(pt.position.x, pt.position.y, pt.position.z);
		}
	}
	//Transposed on the way out: the convention every shader reads `world` under.
	XMStoreFloat4x4(&transform->world_matrix, XMMatrixTranspose(transform->world_xmmatrix));
	XMStoreFloat4x4(&transform->world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform->world_xmmatrix)));
}

void ParticleSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = particles.GetData().begin(); it != particles.GetData().end(); ++it)
	{
		if (IsShown(coordinator, *it->base) && (it->base->scene_visible || it->base->draw_method == DRAW_ALWAYS)) {
			if (it->owns_matrix) {
				ComposeWorldMatrix(*it);
			}
			for (auto& p : it->particles->data.GetData()) {
				p->Update(elapsed_nsec, total_nsec);
			}
		}
	}
}

