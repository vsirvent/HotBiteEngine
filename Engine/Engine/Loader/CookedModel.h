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

#include <cstdint>
#include <string>
#include <vector>
#include <Defines.h>
#include <Core/Vertex.h>
#include <Core/Mesh.h>

namespace HotBite {
	namespace Engine {
		namespace Loader {
			// == Cooked models ==========================================================
			//
			// An .fbx is a *source* format: reading one means running the FBX SDK, which
			// is slow and cannot run on more than one thread, and then redoing the same
			// derivations on every start - cloning a vertex per polygon corner, the
			// tangents, the skeleton's keyframes sampled one joint at a time through
			// EvaluateGlobalTransform. None of that changes until the file does.
			//
			// A CookedModel is the result of that work, in the form the engine consumes
			// it: the vertices exactly as MeshData::Init takes them, the skeletons with
			// their keyframes already sampled, the collision triangles, the node tree.
			// It is plain data - no SDK object, no GPU resource, no ECS state - so it can
			// be written once to disk and then read back on any thread, which is the
			// whole point: a game with thirty models reads thirty files at once.
			//
			// There is ONE install path. Loading an .fbx extracts a CookedModel and
			// installs it, and loading a cooked file reads one and installs it, so the two
			// cannot drift - World::InstallCookedModel is the only code that turns this
			// data into meshes, entities and shapes.
			//
			// The file lives beside its source (`<source>.cooked`) and is a cache, never
			// the source of truth: it is rebuilt when the source's size or time differ
			// from what it recorded, when the cook version moved, or when the loader
			// options it was made with differ. With no source on disk it is trusted
			// as-is, which is what lets a shipped game carry only cooked files.

			struct CookedMaterial {
				std::string name;
				float4 diffuse_color = {};
				std::string diffuse_texture;
			};

			struct CookedMesh {
				std::string name;
				// The import-time smoothing default (the ".NoSmooth" suffix / "smooth"
				// property); see MeshData::smooth.
				bool smooth = true;
				// The *flat* vertices, one frame per polygon corner, plus the control point
				// each came from - MeshData::Init fuses them, and keeps both halves so the
				// smoothing stays a flag that can be flipped later.
				std::vector<Core::Vertex> vertices;
				std::vector<uint32_t> indices;
				std::vector<uint32_t> smooth_groups;
				// The skin, as a skeleton's joints in index order (Skeleton::CpuData()).
				bool skinned = false;
				std::vector<Core::JointCpuData> joints;
			};

			struct CookedShape {
				std::string name;
				float3 authored_scale = { 1.0f, 1.0f, 1.0f };
				std::vector<float3> vertices;
				std::vector<float3> normals;
				std::vector<uint32_t> indices;
			};

			// An animation set: the clips of an .fbx's skeleton, keyed by what
			// World::GetSkeletons() registers it under.
			struct CookedSkeleton {
				std::string name;
				std::vector<Core::JointCpuData> joints;
			};

			// One node of the .fbx scene that becomes an entity. Nodes are in depth-first
			// order, so a parent always precedes its children and `parent` is an index
			// into the same list.
			struct CookedNode {
				enum class Kind : uint8_t { Empty, Mesh, DirectionalLight, AmbientLight, PointLight, Camera };
				std::string name;
				int32_t parent = -1;
				Kind kind = Kind::Empty;
				float3 position = {};
				float3 scale = { 1.0f, 1.0f, 1.0f };
				float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
				std::string mesh;      // Kind::Mesh: the CookedMesh it draws
				std::string material;  // the node's first material, empty for none
				float3 light_color = {};
				float light_intensity = 0.0f;
			};

			struct CookedModel {
				// Bumped whenever extraction changes what it produces, or this layout does.
				// A file of another version is not read, it is made again.
				static constexpr uint32_t VERSION = 1;
				static constexpr const char* EXTENSION = ".cooked";

				// What the file was made with: the loader options that change the result.
				bool triangulate = false;
				bool use_animation_names = false;
				// The source's size and last-write time when it was cooked (0/0: unknown).
				uint64_t source_size = 0;
				int64_t source_time = 0;

				std::vector<CookedMaterial> materials;
				std::vector<CookedMesh> meshes;
				std::vector<CookedShape> shapes;
				std::vector<CookedSkeleton> skeletons;
				std::vector<CookedNode> nodes;

				// Where the cooked file of `source` is.
				static std::string PathFor(const std::string& source);
				// `source`'s size and last-write time. False when it cannot be read.
				static bool StampOf(const std::string& source, uint64_t& size, int64_t& time);

				bool Write(const std::string& file, std::string* error = nullptr) const;
				// Reads a whole cooked file. Never throws and never trusts it: a truncated or
				// corrupt file is a false return and a reason, not a crash. Does not look at
				// the source - IsCurrentFor does.
				static bool Read(const std::string& file, CookedModel& out, std::string* error = nullptr);
				// Whether this (already read) model is still what loading `source` with
				// these options would produce.
				bool IsCurrentFor(const std::string& source, bool triangulate, bool use_animation_names) const;
			};
		}
	}
}
