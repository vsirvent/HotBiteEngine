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

#include <reactphysics3d/reactphysics3d.h>
#include <Defines.h>
#include <vector>
#include <memory>
#include <fbxsdk.h>
#include <Core/Vertex.h>
#include <Core/Utils.h>
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Physics.h>
#include <Components/Lights.h>
#include <ECS/Coordinator.h>

#pragma comment(lib, "libfbxsdk.lib")

namespace HotBite {
	namespace Engine {
		namespace Loader {
			class FBXLoader
			{
			private:
				FbxManager* fbx_manager = nullptr;
				FbxNode* child_node = nullptr;
				FbxScene* scene = nullptr;
				FbxAnimEvaluator* evaluator = nullptr;
				int anim_stack_count = 0;
				FbxTime time;

				float  GetMaterialProperty(const FbxSurfaceMaterial* pMaterial, const char* pPropertyName);
				float4  GetMaterialProperty(const FbxSurfaceMaterial* pMaterial, const char* pPropertyName, const char* pFactorPropertyName, std::string* pTextureName);

				Core::Vertex MixSimilarVertices(const std::vector<Core::Vertex>& vertices, const std::unordered_map<int, std::vector<int>>& cloned_vertices,
					int id, const std::vector<int> child_ids, std::unordered_map<int, bool>& used_vertices, std::vector<int>& ids);

				void CalculateTangents(std::vector<Core::Vertex>& vertices, const std::vector<unsigned int>& indices);

				void InitializeSdkObjects();
				void DestroySdkObjects();
				void SaveScene(std::string filename);

				bool ProcessLight(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node);
				bool ProcessCamera(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node);
				bool ProcessMesh(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node);
				bool ProcessShape(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node);

				void LoadAnimations(std::shared_ptr<Core::Skeleton> skeleton, FbxNode* node, FbxNode* root_node, const std::string& animation_name = "");
			public:

				FBXLoader();
				~FBXLoader();

				FbxScene* GetScene() { return scene; };
				bool LoadScene(const std::string& file, bool triangulate);

				int LoadSkeletons(const std::string& filename, Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>& skeletons, FbxNode* node, bool use_animation_names = false);
				int LoadMeshes(Core::FlatMap<std::string, Core::MeshData>& meshes, FbxNode* node, Core::VertexBuffer<Core::Vertex>* vb);
				int LoadMaterials(Core::FlatMap<std::string, Core::MaterialData>& materials, FbxNode* node);
				int LoadShapes(Core::FlatMap<std::string, Core::ShapeData>& shapes, FbxNode* node);

				ECS::Entity ProcessEntity(Core::FlatMap<std::string, Core::MeshData>& meshes,
					Core::FlatMap<std::string, Core::MaterialData>& materials,
					Core::FlatMap<std::string, Core::ShapeData>& shapes,
					ECS::Coordinator* coordinator, FbxNode* node, ECS::Entity parent = ECS::INVALID_ENTITY_ID);
			};
		}
	}
}

