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
#include <functional>
#include "CookedModel.h"

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

				void CalculateTangents(std::vector<Core::Vertex>& vertices, const std::vector<unsigned int>& indices);

				void InitializeSdkObjects();
				void DestroySdkObjects();
				void SaveScene(std::string filename);

				int ExtractSkeletons(const std::string& filename, CookedModel& out, FbxNode* node, bool use_animation_names);
				int ExtractMeshes(CookedModel& out, FbxNode* node);
				int ExtractMaterials(CookedModel& out, FbxNode* node);
				int ExtractShapes(CookedModel& out, FbxNode* node);
				int ExtractNodes(CookedModel& out, FbxNode* node, int32_t parent);
				bool LoadScene(const std::string& file, bool triangulate);

				void LoadAnimations(std::shared_ptr<Core::Skeleton> skeleton, FbxNode* node, FbxNode* root_node, const std::string& animation_name = "");
			public:

				FBXLoader();
				~FBXLoader();

				// Reads the scene into a CookedModel - the data the engine installs, and the
				// file a cook writes. Nothing here touches a World, a device or the physics
				// library, which is what lets a command-line cook run without any of them.
				// False when the file cannot be read. `on_progress` gets the same coarse phases
				// World::LoadFBX has always reported.
				bool Extract(const std::string& filename, bool triangulate, bool use_animation_names, CookedModel& out,
					std::function<void(float, const std::string&)> on_progress = nullptr);
			};
		}
	}
}

