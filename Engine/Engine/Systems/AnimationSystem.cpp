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
#include "AnimationSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace DirectX;

void AnimationMeshSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	signature.set(coordinator->GetComponentType<Mesh>(), true);
	signature.set(coordinator->GetComponentType<Base>(), true);
}


void AnimationMeshSystem::OnEntityDestroyed(ECS::Entity entity) {
	meshes.Remove(entity);
}

void AnimationMeshSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {

	if ((entity_signature & signature) == signature)
	{
		MeshEntity mesh{ coordinator, entity };
		meshes.Insert(entity, mesh);
	}
	else
	{
		meshes.Remove(entity);
	}
}

void AnimationMeshSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = meshes.GetData().begin(); it != meshes.GetData().end(); ++it)
	{
		if (it->base->visible && it->base->scene_visible) {
			it->mesh->Update(elapsed_nsec, total_nsec);
		}
	}
}

