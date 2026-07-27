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
#include "Material.h"
#include <memory>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;


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

//Applies `smooth` to `vertices` from the flat frames and the grouping. Returns false
//when there is nothing to apply, which is how a mesh loaded without grouping (the
//built-in cube, a game building its own geometry) stays pinned to what it was given.
//
//The two halves are deliberately separate. Fusing the *frames* is the smoothing, and
//is what the flag turns off. Propagating the skin weights is not optional: only the
//control point carries the weights the importer read out of the cluster, its clones
//are created before any of that exists, so without this pass every cloned vertex of a
//skinned mesh renders unskinned. That used to ride along inside the smoothing pass,
//which meant a flat-shaded skinned mesh came out broken - invisible only because
//nothing had ever asked for one.
static bool ApplySmoothing(std::vector<Vertex>& vertices,
	const std::vector<MeshData::VertexFrame>& flat_frames,
	const std::vector<uint32_t>& smooth_groups, bool smooth) {
	if (flat_frames.size() != vertices.size() || smooth_groups.size() != vertices.size()) {
		return false;
	}
	//The sum over each group, indexed by the group's control point. Summed and not
	//averaged, which is what the importer always did: the shaders normalize, so the
	//length never reaches the surface, and averaging here would change every lit
	//pixel of every existing scene for nothing.
	std::vector<MeshData::VertexFrame> fused;
	if (smooth) {
		fused.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			MeshData::VertexFrame& f = fused[smooth_groups[i]];
			const MeshData::VertexFrame& src = flat_frames[i];
			f.normal.x += src.normal.x;    f.normal.y += src.normal.y;    f.normal.z += src.normal.z;
			f.tangent.x += src.tangent.x;  f.tangent.y += src.tangent.y;  f.tangent.z += src.tangent.z;
			f.bitangent.x += src.bitangent.x; f.bitangent.y += src.bitangent.y; f.bitangent.z += src.bitangent.z;
		}
	}
	for (size_t i = 0; i < vertices.size(); ++i) {
		const uint32_t group = smooth_groups[i];
		const MeshData::VertexFrame& f = smooth ? fused[group] : flat_frames[i];
		vertices[i].Normal = f.normal;
		vertices[i].Tangent = f.tangent;
		vertices[i].Bitangent = f.bitangent;
		if (group != (uint32_t)i) {
			memcpy(vertices[i].Boneids, vertices[group].Boneids, sizeof(vertices[i].Boneids));
			vertices[i].Weights = vertices[group].Weights;
		}
	}
	return true;
}

void MeshData::Init(VertexBuffer<Core::Vertex>* vb, const std::string& mesh_name, const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices, std::shared_ptr<Skeleton> skeleton,
	const std::vector<uint32_t>* smooth_groups, bool smooth)
{
	if (skeleton != nullptr) {
		skeletons.push_back(skeleton);
	}
	this->name = mesh_name;
	this->init = true;
	this->vertices = vertices;
	this->indices = indices;
	this->vertex_buffer = vb;
	this->smooth = smooth;

	//Held before anything fuses them, so the flat shading stays recoverable.
	if (smooth_groups != nullptr && smooth_groups->size() == vertices.size()) {
		this->smooth_groups = *smooth_groups;
		flat_frames.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			flat_frames[i] = { vertices[i].Normal, vertices[i].Tangent, vertices[i].Bitangent };
		}
		//Before the boxes below and before the upload: BuildSkinnedBoxes reads the
		//bone ids this pass hands to the cloned vertices.
		ApplySmoothing(this->vertices, flat_frames, this->smooth_groups, smooth);
	}

	bvh.Init(this->vertices, indices);

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
	vb->AddMesh(this->vertices, indices, &vertexOffset, &indexOffset);

	BuildSkinnedBoxes();
}

bool MeshData::SetSmooth(bool enable) {
	if (enable == smooth) {
		return false;
	}
	if (!ApplySmoothing(vertices, flat_frames, smooth_groups, enable)) {
		//No grouping was recorded for this mesh, so there is nothing to fuse or
		//unfuse. Leaving `smooth` alone keeps the readout honest about what is on
		//screen rather than reporting a change that never happened.
		return false;
	}
	smooth = enable;
	if (vertex_buffer != nullptr) {
		vertex_buffer->UpdateMesh(vertexOffset, vertices);
	}
	//Nothing else is re-derived on purpose. Only the vertex frames move; positions,
	//bone ids and weights are the same either way, so the BVH, the dimensions and the
	//joint/animation boxes all still describe this mesh.
	return true;
}

//A vertex counts towards a joint's box when that joint carries at least this share of
//the influence the vertex's strongest joint has.
//
//Not every joint that touches it: skinning spreads small weights a long way, and a
//chest vertex with 0.1 of a forearm in it puts the whole chest into the forearm's box -
//which is then swung out to wherever the arm hangs. Counting every non-zero weight
//measured the demo troll's idle box at 16.5 world units across; with this threshold it
//is 13.1, for a model whose silhouette in that pose is 9.7 (and whose bind pose, which
//is what the box used to be measured from, is 21.5). Dropping to the single dominant
//joint only takes another 2% off, so half of the dominant weight is the line: the pair
//blending a seam both stay in (each is at least half of the other), the long-range
//trickle drops out, and nothing is gained by cutting closer.
//
//The union is then no longer a strict bound - a vertex sitting at 0.6/0.2/0.2 is placed
//by the 0.6 alone - but it is off by what those small weights displace it by, which is
//a fraction of one joint's own motion.
static constexpr float JOINT_BOX_WEIGHT_SHARE = 0.5f;

//The box of the vertices each joint moves, in the space they are stored in - which
//is the space the skinning matrices expect, so a posed box is then a matter of
//transforming these and taking the union. One pass over the vertices, once.
static void MeasureJointBoxes(const std::vector<Vertex>& vertices,
	std::vector<OptionalBox>& joint_boxes, OptionalBox& static_box) {
	joint_boxes.clear();
	static_box = OptionalBox{};
	int njoints = 0;
	for (const Vertex& v : vertices) {
		for (int i = 0; i < 4; ++i) {
			njoints = (std::max)(njoints, v.Boneids[i] + 1);
		}
	}
	std::vector<vector3d> mins((size_t)njoints, XMVectorReplicate(FLT_MAX));
	std::vector<vector3d> maxs((size_t)njoints, XMVectorReplicate(-FLT_MAX));
	std::vector<bool> used((size_t)njoints, false);
	vector3d static_min = XMVectorReplicate(FLT_MAX);
	vector3d static_max = XMVectorReplicate(-FLT_MAX);
	for (const Vertex& v : vertices) {
		const vector3d p = XMVectorSet(v.Position.x, v.Position.y, v.Position.z, 1.0f);
		bool skinned = false;
		const float* weights = &v.Weights.x;
		//Exactly the test the vertex shader makes before blending a joint in, so a
		//vertex is "skinned" here if and only if the shader moves it at all.
		float dominant = 0.0f;
		for (int i = 0; i < 4; ++i) {
			if (v.Boneids[i] < 0 || v.Boneids[i] >= njoints || weights[i] <= 0.0f) {
				continue;
			}
			skinned = true;
			dominant = (std::max)(dominant, weights[i]);
		}
		for (int i = 0; i < 4; ++i) {
			const int id = v.Boneids[i];
			if (id < 0 || id >= njoints ||
				weights[i] < dominant * JOINT_BOX_WEIGHT_SHARE || weights[i] <= 0.0f) {
				continue;
			}
			mins[id] = XMVectorMin(mins[id], p);
			maxs[id] = XMVectorMax(maxs[id], p);
			used[id] = true;
		}
		if (!skinned) {
			//No joint drives it: the shader draws it where it is stored, in every pose.
			static_min = XMVectorMin(static_min, p);
			static_max = XMVectorMax(static_max, p);
			static_box.valid = true;
		}
	}
	joint_boxes.resize((size_t)njoints);
	for (int j = 0; j < njoints; ++j) {
		if (used[j]) {
			box::CreateFromPoints(joint_boxes[j].bounds, mins[j], maxs[j]);
			joint_boxes[j].valid = true;
		}
	}
	if (static_box.valid) {
		box::CreateFromPoints(static_box.bounds, static_min, static_max);
	}
}

//Grows `lo`/`hi` to contain `b`.
static void UnionBox(const box& b, vector3d& lo, vector3d& hi) {
	const vector3d center = XMVectorSet(b.Center.x, b.Center.y, b.Center.z, 0.0f);
	const vector3d extents = XMVectorSet(b.Extents.x, b.Extents.y, b.Extents.z, 0.0f);
	lo = XMVectorMin(lo, XMVectorSubtract(center, extents));
	hi = XMVectorMax(hi, XMVectorAdd(center, extents));
}

void MeshData::BuildSkinnedBoxes() {
	MeasureJointBoxes(vertices, joint_boxes, static_box);
	animation_boxes.assign(skeletons.size(), {});
	if (joint_boxes.empty() || skeletons.empty() || skeletons[0] == nullptr) {
		return;
	}
	//The bind-pose matrices come from the first skeleton, which is where
	//Components::Mesh::GetAnimationMatrix reads them from as well: a set imported from
	//a second FBX supplies keyframes for the same rig, not a rig of its own. Taking
	//them from anywhere else would put these boxes in a pose the vertex shader never
	//draws.
	const std::vector<JointCpuData>& bind = skeletons[0]->CpuData();
	for (size_t s = 0; s < skeletons.size(); ++s) {
		if (skeletons[s] == nullptr) {
			continue;
		}
		const std::vector<JointCpuData>& cpu = skeletons[s]->CpuData();
		int nanims = 0;
		for (const JointCpuData& joint : cpu) {
			nanims = (std::max)(nanims, (int)joint.animations.size());
		}
		std::vector<OptionalBox>& boxes = animation_boxes[s];
		boxes.assign((size_t)nanims, OptionalBox{});
		for (int anim = 0; anim < nanims; ++anim) {
			vector3d lo = XMVectorReplicate(FLT_MAX);
			vector3d hi = XMVectorReplicate(-FLT_MAX);
			bool any = false;
			for (size_t j = 0; j < cpu.size() && j < joint_boxes.size() && j < bind.size(); ++j) {
				if (!joint_boxes[j].valid || anim >= (int)cpu[j].animations.size()) {
					continue;
				}
				const matrix bp = XMLoadFloat4x4(&bind[j].model_to_bindpose);
				for (const Keyframe& k : cpu[j].animations[anim].key_frames) {
					//The same product the animation blend builds every frame: model space
					//-> this joint's bind pose -> where the keyframe puts the joint.
					box posed;
					joint_boxes[j].bounds.Transform(posed, bp * XMLoadFloat4x4(&k.transform));
					UnionBox(posed, lo, hi);
					any = true;
				}
			}
			if (!any) {
				continue;
			}
			if (static_box.valid) {
				UnionBox(static_box.bounds, lo, hi);
			}
			box::CreateFromPoints(boxes[anim].bounds, lo, hi);
			boxes[anim].valid = true;
		}
	}
}

const box* MeshData::GetAnimationBox(const Skeleton* skeleton, int animation_id) const {
	if (skeleton == nullptr || animation_id < 0) {
		return nullptr;
	}
	for (size_t s = 0; s < skeletons.size() && s < animation_boxes.size(); ++s) {
		if (skeletons[s].get() != skeleton) {
			continue;
		}
		const std::vector<OptionalBox>& boxes = animation_boxes[s];
		if (animation_id >= (int)boxes.size() || !boxes[animation_id].valid) {
			return nullptr;
		}
		return &boxes[animation_id].bounds;
	}
	return nullptr;
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
	//Indexed by skeleton, so they mean nothing once those are gone.
	animation_boxes.clear();
	joint_boxes.clear();
	static_box = OptionalBox{};
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
		//A set attached after Init brings animations of its own, and their boxes are
		//what any entity playing them measures its Bounds with.
		BuildSkinnedBoxes();
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
