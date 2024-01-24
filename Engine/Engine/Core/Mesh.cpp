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

#include "Mesh.h"
#include "DXCore.h"
#include "Utils.h"
#include <memory>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;


std::unordered_map<int, std::string> Skeleton::GetAnimations() {
	std::unordered_map<int, std::string> animations;
	for (int i = 0; i < joint_cpu_data.size(); ++i) {
		for (int n = 0; n < joint_cpu_data[i].animations.size(); ++n) {
			JointAnim* animation = &(joint_cpu_data[i].animations[n]);
			if (animation->key_frames.size() > 0) {
				if (animations.find(i) != animations.end()) {
					animations[i] = animation->name;
				}
			}
		}
	}
	return animations;
}

MeshData::MeshData() {
}

MeshData::~MeshData() {	
}

void MeshData::Init(VertexBuffer<Core::Vertex>* vb, const std::string& mesh_name, const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices, std::shared_ptr<Skeleton> skeleton)
{
	if (skeleton != nullptr) {
		skeletons.push_back(skeleton);
	}
	this->name = mesh_name;
	this->init = true;
	this->vertices = vertices;
	this->indices = indices;

	bvh.Init(vertices, indices);

	//Calculate max and min dimensions
	indexCount = (uint32_t)indices.size();
	vertexCount = (uint32_t)vertices.size();
	minDimensions = float3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxDimensions = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (uint32_t i = 0; i < vertexCount; ++i)
	{
		auto pos = vertices[i].Position;
		if (pos.x < minDimensions.x)minDimensions.x = pos.x;
		if (pos.y < minDimensions.y)minDimensions.y = pos.y;
		if (pos.z < minDimensions.z)minDimensions.z = pos.z;
		if (pos.x > maxDimensions.x)maxDimensions.x = pos.x;
		if (pos.y > maxDimensions.y)maxDimensions.y = pos.y;
		if (pos.z > maxDimensions.z)maxDimensions.z = pos.z;
	}
	vb->AddMesh(vertices, indices, &vertexOffset, &indexOffset);
}

void MeshData::LoadTextures() {
	if (normal_map != nullptr) {
		normal_map->Release();
	}
	if (!mesh_normal_texture.empty()) {
		normal_map = LoadTexture(this->mesh_normal_texture);
	}
	else {
		normal_map = nullptr;
	}
}

void MeshData::Release() {
	if (normal_map != nullptr) {
		normal_map->Release();
		normal_map = nullptr;
	}	
	skeletons.clear();
	init = false;
}

void MeshData::AddSkeleton(std::shared_ptr<Skeleton> skl) {
	bool found = false;
	for (const auto& s : skeletons) {
		if (s == skl) {
			found = true;
			break;
		}
	}
	if (!found) {
		skeletons.push_back(skl);
	}
}

std::unordered_map<int, std::string> MeshData::GetAnimations() {
	std::unordered_map<int, std::string> skeleton_animations;
	for (int i = 0; i < skeletons.size(); ++i) {
		std::shared_ptr<Skeleton> skl = skeletons[i];
		std::unordered_map<int, std::string> animations = skl->GetAnimations();
		for (auto& anim : animations) {
			int id = (i & 0xFFFF) << 16 | (anim.first & 0xFFFF);
			skeleton_animations[id] = anim.second;
		}
	}
	return skeleton_animations;
}
