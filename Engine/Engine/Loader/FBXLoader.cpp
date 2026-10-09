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
#include <Core/PhysicsCommon.h>
// Must come before the FBX SDK headers below: fbxarch.h does `#define snprintf
// _snprintf` and fbxmath.h `#define isnan _isnan`, and nlohmann's json calls
// std::snprintf/std::isnan - which those macros turn into std::_snprintf and
// std::_isnan, neither of which exists. Json.h arrives here indirectly (component
// headers pull it in for ToJson/FromJson), so it is included up front where the
// macros are not defined yet rather than left to whichever header gets there first.
#include <Core/Json.h>
#include "Defines.h"
#include "FBXUtil.h"
#include "FBXLoader.h"
#include <stdio.h>
#include <Core/Mesh.h>
#include <Core/BVH.h>

using namespace std;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Loader;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::FBX;
using namespace HotBite::Engine::Components;
using namespace DirectX;

#define RATIO 100.0f

#ifdef IOS_REF
#undef  IOS_REF
#define IOS_REF (*(fbx_manager->GetIOSettings()))
#endif


FBXLoader::FBXLoader()
{
	InitializeSdkObjects();
}

FBXLoader::~FBXLoader()
{
	DestroySdkObjects();
}

void FBXLoader::InitializeSdkObjects()
{
	fbx_manager = FbxManager::Create();
	assert(fbx_manager && "Error: Unable to create FBX Manager!\n");

	printf("Autodesk FBX SDK version %s\n", fbx_manager->GetVersion());

	FbxIOSettings* ios = FbxIOSettings::Create(fbx_manager, IOSROOT);
	fbx_manager->SetIOSettings(ios);

	FbxString path = FbxGetApplicationDirectory();
	fbx_manager->LoadPluginsDirectory(path.Buffer());

	scene = FbxScene::Create(fbx_manager, "scene");
	assert(scene && "Error: Unable to create FBX scene!\n");
}

void FBXLoader::DestroySdkObjects()
{
	if (fbx_manager) fbx_manager->Destroy();
}

void FBXLoader::SaveScene(std::string filename) {
	// Create an IOSettings object.
	size_t lastindex = filename.find_last_of(".");
	filename = filename.substr(0, lastindex);
	FbxIOSettings* ios = FbxIOSettings::Create(fbx_manager, IOSROOT);
	fbx_manager->SetIOSettings(ios);

	// Create an exporter.
	FbxExporter* exporter = FbxExporter::Create(fbx_manager, "");

	// Initialize the exporter.
	exporter->Initialize(filename.c_str(), -1, exporter->GetIOSettings());
	exporter->Export(scene);
	exporter->Destroy();
}

void FBXLoader::LoadAnimations(std::shared_ptr<Core::Skeleton> skeleton, FbxNode* node, FbxNode* root_node, const std::string& animation_name) {
	if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		Core::Joint joint;
		//if (strstr(node->GetName(), "_end") != nullptr) {
		//	return;
		//}
		joint.cpu_data.name = node->GetName();
		joint.cpu_data.joint_id = (int)node->GetUniqueID();

		FbxNode* oparent = dynamic_cast<FbxNode*>(node->GetParent());
		int parent_id = -1;
		if (oparent != nullptr && oparent->GetNodeAttribute() && oparent->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
			parent_id = (int)oparent->GetUniqueID();
		}

		if (anim_stack_count > 0) {
			for (int stack = 0; stack < anim_stack_count; ++stack) {
				Core::JointAnim animation;
				FbxAnimStack* currAnimStack = scene->GetSrcObject<FbxAnimStack>(stack);
				FbxTimeSpan interval;
				scene->SetCurrentAnimationStack(currAnimStack);
				node->GetAnimationInterval(interval, currAnimStack);
				FbxTime start = interval.GetStart();
				FbxTime stop = interval.GetStop();
				float start_msec = (float)start.GetMilliSeconds();
				float stop_msec = (float)stop.GetMilliSeconds();

				animation.fps = 5.0f;
				animation.start = (float)start_msec;
				animation.end = (float)stop_msec;
				animation.duration = animation.end - animation.start;

				if (!animation_name.empty()) {
					animation.name = animation_name;
				}
				else {
					animation.name = currAnimStack->GetName();
					size_t pos = animation.name.find_last_of("|");
					if (pos != std::string::npos) {
						animation.name = animation.name.substr(pos + 1);
					}
				}

				float step = 1000.0f / animation.fps;

				int kid = 0;
				for (float t = start_msec; t <= stop_msec; t += step) {
					Core::Keyframe k;
					k.id = kid++;
					FbxTime key_time((uint64_t)t * FBXSDK_TC_MILLISECOND);
					FbxAMatrix global_transform = node->EvaluateGlobalTransform(key_time);
					FbxAMatrix currentTransformOffset = root_node->EvaluateGlobalTransform(key_time);
					FbxAMatrix transform = currentTransformOffset.Inverse() * global_transform;
					k.transform = getMatrix(transform);
					animation.key_frames.emplace_back(std::move(k));
				}
				joint.cpu_data.animations.emplace_back(std::move(animation));
			}
		}
		//reset animation stack
		scene->SetCurrentAnimationStack(scene->GetSrcObject<FbxAnimStack>(0));
		skeleton->AddJoint(joint, parent_id);
		//Load node childs
		for (int i = 0; i < node->GetChildCount(); ++i) {
			FbxNode* child_node = node->GetChild(i);
			LoadAnimations(skeleton, child_node, root_node, animation_name);
		}
		//reset animation stack
		scene->SetCurrentAnimationStack(scene->GetSrcObject<FbxAnimStack>(0));
	}
}

int FBXLoader::ExtractSkeletons(const std::string& filename, CookedModel& out, FbxNode* node, bool use_animation_names) {
	//Load skeleton animations
	int ret = 0;
	if (node->GetNodeAttribute() != nullptr && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		std::string name = filename;
		std::size_t path = name.find_last_of("\\");
		if (path == std::string::npos) {
			path = name.find_last_of("/");
		}
		if (path != std::string::npos) {
			name = name.substr(path + 1);
		}
		std::size_t ext = name.find_last_of(".");
		if (ext != std::string::npos) {
			name = name.substr(0, ext);
		}
		std::shared_ptr<Skeleton> skl = make_shared<Skeleton>();
		LoadAnimations(skl, node, node->GetParent(), use_animation_names ? "" : name);
		skl->Flush();
		printf("Skeleton with %llu bones loaded\n", skl->CpuData().size());
		out.skeletons.push_back({ name, skl->CpuData() });
		++ret;
	}
	else {
		//Load node childs
		for (int i = 0; i < node->GetChildCount(); ++i) {
			FbxNode* child_node = node->GetChild(i);
			ret += ExtractSkeletons(filename, out, child_node, use_animation_names);
		}
	}
	return ret;
}

int FBXLoader::ExtractMeshes(CookedModel& out, FbxNode* node) {
	int ret = 0;
	if (node->GetNodeAttribute() != nullptr && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		std::string name = node->GetName();
		std::vector<unsigned int> indices;
		std::unordered_map<int, bool> used_vertices;
		//Per vertex, the control point it came from. Handed to MeshData so smoothing
		//stays a decision the engine can remake later rather than one baked in here -
		//see the comment on MeshData::flat_frames.
		std::vector<uint32_t> smooth_groups;

		FbxMesh* fbxMesh = (FbxMesh*)node->GetNodeAttribute();
		FbxVector4* controlPoints = fbxMesh->GetControlPoints();
		int vertexCount = fbxMesh->GetControlPointsCount();
		std::vector<Vertex> vertices;

		//The import-time default only. It is what the mesh loads as; from there the
		//Mesh component's "smooth" flag overrides it per level, which is why the
		//".NoSmooth" suffix is still honoured - it is how every existing .fbx says
		//this, and the models cannot be re-exported to say it any other way.
		bool smooth = true;
		FbxProperty p = node->FindProperty("smooth", false);
		if (p.IsValid())
		{
			smooth = (bool)p.Get<int>();
		}
		if (name.find(".NoSmooth") != std::string::npos) {
			smooth = false;
		}

		Vertex v = {};
		for (int i = 0; i < vertexCount; i++)
		{
			//Blender
			v.Position.x = (float)controlPoints[i].mData[0];
			v.Position.y = (float)controlPoints[i].mData[1];
			v.Position.z = (float)controlPoints[i].mData[2];
			v.Normal = {};
			vertices.push_back(v);
			//An original is its own group; clones below join it.
			smooth_groups.push_back((uint32_t)i);
		}

		int polygonCount = fbxMesh->GetPolygonCount();
		for (int i = 0; i < polygonCount; i++)
		{
			int polygonSize = fbxMesh->GetPolygonSize(i);
			for (int j = 0; j < polygonSize; j++)
			{
				const int control_point = fbxMesh->GetPolygonVertex(i, j);
				int ind = control_point;
				if (used_vertices[ind] == true) {
					vertices.push_back(vertices[ind]);
					smooth_groups.push_back((uint32_t)control_point);
					ind = (int)vertices.size() - 1;
				}
				indices.push_back(ind);

				FbxVector4 norm(0, 0, 0, 0);
				fbxMesh->GetPolygonVertexNormal(i, j, norm);
				vertices[ind].Normal.x = (float)norm.mData[0];
				vertices[ind].Normal.y = (float)norm.mData[1];
				vertices[ind].Normal.z = (float)norm.mData[2];

				FbxVector2 uvCoord(0, 0);
				bool uvFlag = false;
				if (fbxMesh->GetPolygonVertexUV(i, j, "UVMap", uvCoord, uvFlag)) {
					vertices[ind].UV.x = (float)uvCoord.mData[0];
					vertices[ind].UV.y = 1.0f - (float)uvCoord.mData[1];
				}
				if (fbxMesh->GetPolygonVertexUV(i, j, "MeshUVMap", uvCoord, uvFlag)) {
					vertices[ind].MeshUV.x = (float)uvCoord.mData[0];
					vertices[ind].MeshUV.y = 1.0f - (float)uvCoord.mData[1];
				}
				used_vertices[ind] = true;
			}
		}
		used_vertices.clear();

		CalculateTangents(vertices, indices);

		shared_ptr<Core::Skeleton> skeleton = nullptr;
		//Load bones
		if (fbxMesh->GetDeformerCount() > 0)
		{
			skeleton = std::make_shared<Core::Skeleton>();
			FbxSkin* skin = reinterpret_cast<FbxSkin*>(fbxMesh->GetDeformer(0, FbxDeformer::eSkin));
			unsigned int numOfClusters = skin->GetClusterCount();

			for (unsigned int clusterIndex = 0; clusterIndex < numOfClusters; ++clusterIndex)
			{
				Core::Joint joint;
				FbxCluster* currCluster = skin->GetCluster(clusterIndex);

				joint.cpu_data.name = currCluster->GetLink()->GetName();

				joint.cpu_data.joint_id = (int)currCluster->GetLink()->GetUniqueID();

				FbxNode* oparent = dynamic_cast<FbxNode*>(currCluster->GetLink()->GetParent());
				int parent_id = -1;
				if (oparent != nullptr && oparent->GetNodeAttribute() && oparent->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
					parent_id = (int)oparent->GetUniqueID();
				}
				FbxAMatrix transformMatrix;
				FbxAMatrix transformLinkMatrix;
				FbxAMatrix globalBindposeInverseMatrix;

				// Transform link matrix.
				currCluster->GetTransformLinkMatrix(transformLinkMatrix);
				// The transformation of the mesh at binding time
				currCluster->GetTransformMatrix(transformMatrix);

				// Inverse bind matrix.
				globalBindposeInverseMatrix = transformLinkMatrix.Inverse() * transformMatrix;
				joint.cpu_data.model_to_bindpose = getMatrix(globalBindposeInverseMatrix);

				//Now load joint animations
				if (anim_stack_count > 0) {
					for (int stack = 0; stack < anim_stack_count; ++stack) {
						Core::JointAnim animation;
						FbxAnimStack* currAnimStack = scene->GetSrcObject<FbxAnimStack>(stack);
						FbxTimeSpan interval;
						scene->SetCurrentAnimationStack(currAnimStack);
						currCluster->GetLink()->GetAnimationInterval(interval, currAnimStack);
						FbxTime start = interval.GetStart();
						FbxTime stop = interval.GetStop();
						float start_msec = (float)start.GetMilliSeconds();
						float stop_msec = (float)stop.GetMilliSeconds();

						animation.fps = 4.0f;
						animation.start = (float)start_msec;
						animation.end = (float)stop_msec;
						animation.duration = animation.end - animation.start;

						animation.name = currAnimStack->GetName();
						size_t pos = animation.name.find_last_of("|");
						if (pos != std::string::npos) {
							animation.name = animation.name.substr(pos + 1);
						}

						float step = 1000.0f / animation.fps;

						int kid = 0;
						for (float t = start_msec; t <= stop_msec; t += step) {
							Core::Keyframe k;
							k.id = kid++;
							FbxTime key_time((uint64_t)t * FBXSDK_TC_MILLISECOND);
							FbxAMatrix currentTransformOffset = node->EvaluateGlobalTransform(key_time);
							FbxAMatrix global_transform = currCluster->GetLink()->EvaluateGlobalTransform(key_time);
							FbxAMatrix transform = currentTransformOffset.Inverse() * global_transform;
							k.transform = getMatrix(transform);
							animation.key_frames.emplace_back(std::move(k));
						}
						joint.cpu_data.animations.emplace_back(std::move(animation));
						break;
					}
				}
				//reset animation stack
				scene->SetCurrentAnimationStack(scene->GetSrcObject<FbxAnimStack>(0));
				skeleton->AddJoint(joint, parent_id);
			}

			skeleton->Flush();
			printf("Skeleton with %llu bones created\n", skeleton->CpuData().size());
			for (unsigned int clusterIndex = 0; clusterIndex < numOfClusters; ++clusterIndex)
			{
				FbxCluster* currCluster = skin->GetCluster(clusterIndex);
				const Core::Joint* joint = skeleton->GetJointByCpuId((int)currCluster->GetLink()->GetUniqueID());
				if (joint == nullptr) {
					continue;
					//abort();
				}
				int Count = currCluster->GetControlPointIndicesCount();

				for (int i = 0; i < currCluster->GetControlPointIndicesCount(); ++i)
				{
					int index = currCluster->GetControlPointIndices()[i];
					int vertexid = indices[currCluster->GetControlPointIndices()[i]];

					if (vertices[index].Boneids[0] == -1 && vertices[index].Weights.x == -1)
					{
						vertices[index].Boneids[0] = (uint8_t)joint->cpu_data.id;
						vertices[index].Weights.x = (float)currCluster->GetControlPointWeights()[i];
					}
					else if (vertices[index].Boneids[1] == -1 && vertices[index].Weights.y == -1)
					{
						vertices[index].Boneids[1] = (uint8_t)joint->cpu_data.id;
						vertices[index].Weights.y = (float)currCluster->GetControlPointWeights()[i];
					}
					else if (vertices[index].Boneids[2] == -1 && vertices[index].Weights.z == -1)
					{
						vertices[index].Boneids[2] = (uint8_t)joint->cpu_data.id;
						vertices[index].Weights.z = (float)currCluster->GetControlPointWeights()[i];
					}
					else if (vertices[index].Boneids[3] == -1 && vertices[index].Weights.w == -1)
					{
						vertices[index].Boneids[3] = (uint8_t)joint->cpu_data.id;
						vertices[index].Weights.w = (float)currCluster->GetControlPointWeights()[i];
					}
					else
					{
						float currentWeight = (float)currCluster->GetControlPointWeights()[i];
						//Get current lower weight
						float lower_weight = 1.1f;
						int lower_pos = 0;
						float w[4] = { vertices[index].Weights.x, vertices[index].Weights.y, vertices[index].Weights.z, vertices[index].Weights.w };
						for (int i = 0; i < 4; ++i) {
							if (w[i] < lower_weight) {
								lower_weight = w[i];
								lower_pos = i;
							}
						}
						if (currentWeight > lower_weight) {
							vertices[index].Boneids[lower_pos] = (uint8_t)joint->cpu_data.id;
							switch (lower_pos) {
							case 0: vertices[index].Weights.x += currentWeight; break;
							case 1: vertices[index].Weights.y += currentWeight; break;
							case 2: vertices[index].Weights.z += currentWeight; break;
							case 3: vertices[index].Weights.w += currentWeight; break;
							}
						}
					}
				}
			}
		}


		printf("Loaded mesh %s\n", name.c_str());
		//Fusing the clones is MeshData's job (so it can be undone): what is kept is the
		//flat vertices, plus the grouping and the default above.
		CookedMesh mesh;
		mesh.name = name;
		mesh.smooth = smooth;
		mesh.vertices = std::move(vertices);
		mesh.indices.assign(indices.begin(), indices.end());
		mesh.smooth_groups = std::move(smooth_groups);
		if (skeleton != nullptr) {
			mesh.skinned = true;
			mesh.joints = skeleton->CpuData();
		}
		out.meshes.push_back(std::move(mesh));
		++ret;
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		ret += ExtractMeshes(out, child_node);
	}
	return ret;
}

int FBXLoader::ExtractMaterials(CookedModel& out, FbxNode* node) {
	int ret = 0;
	for (int i = 0; i < node->GetMaterialCount(); ++i) {
		FbxSurfaceMaterial* sm = node->GetMaterial(i);
		if (sm == nullptr) {
			continue;
		}
		std::string name = sm->GetName();
		const bool known = std::any_of(out.materials.begin(), out.materials.end(),
			[&name](const CookedMaterial& m) { return m.name == name; });
		if (!known) {
			CookedMaterial m;
			m.name = name;
			//Only the diffuse channel is imported: the engine's material model has no
			//ambient term, so an FBX sAmbient had nowhere to go.
			m.diffuse_color = GetMaterialProperty(sm, FbxSurfaceMaterial::sDiffuse, FbxSurfaceMaterial::sDiffuseFactor, &m.diffuse_texture);
			printf("FBXLoader::Added material %s\n", name.c_str());
			out.materials.push_back(std::move(m));
			++ret;
		}
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		ret += ExtractMaterials(out, child_node);
	}
	return ret;
}

int FBXLoader::ExtractShapes(CookedModel& out, FbxNode* node) {
	int ret = 0;
	if (node->GetNodeAttribute() != nullptr && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		std::string name = node->GetName();
		out.shapes.emplace_back();
		CookedShape* shape = &out.shapes.back();
		shape->name = name;
		FbxAMatrix& matrix = node->EvaluateGlobalTransform();
		FbxVector4 s = matrix.GetS();
		float3 scale = { abs((float)s.mData[0] / (RATIO)), abs((float)s.mData[1] / (RATIO)), abs((float)s.mData[2] / (RATIO)) };
		//Recorded so consumers can tell what scale the baked vertices already carry.
		//Computed identically to the entity's Transform.scale below, so an entity and
		//its own shape always agree exactly.
		shape->authored_scale = scale;
		FbxMesh* fbxMesh = (FbxMesh*)node->GetNodeAttribute();
		FbxVector4* controlPoints = fbxMesh->GetControlPoints();
		int vertexCount = fbxMesh->GetControlPointsCount();
		float3 v = {};

		for (int i = 0; i < vertexCount; i++)
		{
			//Blender
			v.x = (float)controlPoints[i].mData[0] * scale.x;
			v.y = (float)controlPoints[i].mData[1] * scale.y;
			v.z = (float)controlPoints[i].mData[2] * scale.z;
			shape->vertices.push_back(v);
			shape->normals.push_back({ 0.f, 0.f, 0.f });
		}

		int polygonCount = fbxMesh->GetPolygonCount();
		for (int i = 0; i < polygonCount; i++)
		{
			int polygonSize = fbxMesh->GetPolygonSize(i);
			for (int j = 0; j < polygonSize; j++)
			{
				int ind = fbxMesh->GetPolygonVertex(i, j);
				shape->indices.push_back(ind);
				FbxVector4 norm(0, 0, 0, 0);
				fbxMesh->GetPolygonVertexNormal(i, j, norm);
				shape->normals[ind].x += (float)norm.mData[0];
				shape->normals[ind].y += (float)norm.mData[1];
				shape->normals[ind].z += (float)norm.mData[2];
			}
		}
		//The physics shape itself is built when the model is installed
		//(World::InstallCookedModel), not here: it needs the physics library, which a
		//cook (no World, no device) does not have, and the triangles are all it needs.
		printf("Loaded shape %s\n", name.c_str());
		++ret;
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		ret += ExtractShapes(out, child_node);
	}
	return ret;
}

bool FBXLoader::LoadScene(const std::string& filename, bool triangulate)
{
	bool status;
	std::string file_name = filename;
	std::string file_name_tri = filename;
	if (triangulate && file_name_tri.find(".triangles.fbx") == std::string::npos) {
		file_name_tri.append(".triangles.fbx");
	}
	bool create_triangles = false;

	if (triangulate && INVALID_FILE_ATTRIBUTES == GetFileAttributes(file_name_tri.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
	{
		create_triangles = true;
	}
	else {
		create_triangles = false;
		file_name = file_name_tri;
	}

	// Create an importer.
	FbxImporter* importer = FbxImporter::Create(fbx_manager, "");

	// Initialize the importer by providing a filename.
	const bool import_status = importer->Initialize(file_name.c_str(), -1, fbx_manager->GetIOSettings());

	assert(import_status && "Call to FbxImporter::Initialize() failed.");
	if (importer->IsFBX())
	{
		printf("Animation Stack Information\n");

		anim_stack_count = importer->GetAnimStackCount();

		printf("    Number of Animation Stacks: %d\n", anim_stack_count);
		printf("    Current Animation Stack: \"%s\"\n", importer->GetActiveAnimStackName().Buffer());
		printf("\n");

		for (int i = 0; i < anim_stack_count; i++)
		{
			FbxTakeInfo* info = importer->GetTakeInfo(i);

			printf("    Animation Stack %d\n", i);
			printf("         Name: \"%s\"\n", info->mName.Buffer());
			printf("         Description: \"%s\"\n", info->mDescription.Buffer());
			printf("         Import Name: \"%s\"\n", info->mImportName.Buffer());
			printf("         Import State: %s\n", info->mSelect ? "true" : "false");
			printf("\n");
		}

		// Set the import states. By default, the import states are always set to 
		// true. The code below shows how to change these states.
		IOS_REF.SetBoolProp(IMP_FBX_MATERIAL, true);
		IOS_REF.SetBoolProp(IMP_FBX_TEXTURE, true);
		IOS_REF.SetBoolProp(IMP_FBX_LINK, true);
		IOS_REF.SetBoolProp(IMP_FBX_SHAPE, true);
		IOS_REF.SetBoolProp(IMP_FBX_GOBO, true);
		IOS_REF.SetBoolProp(IMP_FBX_ANIMATION, true);
		IOS_REF.SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
	}

	// Import the scene.
	status = importer->Import(scene);
	if (create_triangles) {
		FbxGeometryConverter convert(fbx_manager);
		if (convert.Triangulate(scene, true)) {
			SaveScene(file_name_tri);
		}
		else {
			printf("Triangulation error\n");
		}
	}
	importer->Destroy();
	return status;
}

int FBXLoader::ExtractNodes(CookedModel& out, FbxNode* node, int32_t parent) {
	int ret = 0;
	const FbxNodeAttribute* attribute = node->GetNodeAttribute();
	const FbxNodeAttribute::EType type = attribute != nullptr ? attribute->GetAttributeType() : FbxNodeAttribute::eNull;
	//Skeleton nodes are not entities, and neither is anything below one: the skeleton
	//is loaded as part of the mesh it deforms.
	if (type == FbxNodeAttribute::eSkeleton) {
		return ret;
	}
	CookedNode n;
	n.name = node->GetName();
	n.parent = parent;

	FbxAMatrix& m = node->EvaluateGlobalTransform();
	FbxVector4 t = m.GetT();
	FbxVector4 s = m.GetS();
	FbxQuaternion r = m.GetQ();
	n.position = { (float)t.mData[0] / (RATIO), (float)t.mData[1] / (RATIO), (float)t.mData[2] / (RATIO) };
	n.scale = { abs((float)s.mData[0] / (RATIO)), abs((float)s.mData[1] / (RATIO)), abs((float)s.mData[2] / (RATIO)) };
	n.rotation = { (float)r.mData[0], (float)r.mData[1], (float)r.mData[2], (float)r.mData[3] };

	switch (type) {
	case FbxNodeAttribute::eLight: {
		const FbxLight* fl = (const FbxLight*)attribute;
		const FbxLight::EType light_type = fl->LightType.Get();
		n.light_color = { (float)fl->Color.Get()[0], (float)fl->Color.Get()[1], (float)fl->Color.Get()[2] };
		n.light_intensity = (float)fl->Intensity / (RATIO);
		switch (light_type) {
		case FbxLight::EType::eDirectional: n.kind = CookedNode::Kind::DirectionalLight; break;
		case FbxLight::EType::eArea: n.kind = CookedNode::Kind::AmbientLight; break;
		case FbxLight::EType::ePoint: n.kind = CookedNode::Kind::PointLight; break;
		default: printf("Unknown light type %d\n", (int)light_type); break;
		}
	}break;
	case FbxNodeAttribute::eCamera: {
		n.kind = CookedNode::Kind::Camera;
	}break;
	case FbxNodeAttribute::eMesh: {
		n.kind = CookedNode::Kind::Mesh;
		n.mesh = n.name;
	}break;
	default:
		printf("FBXLoader::ExtractNodes: Node %s, type %d has no components\n", n.name.c_str(), (int)type);
		break;
	}
	//We can only add one material component, even if the FBX file includes multiple
	//materials to the entity, so it is the first that counts.
	for (int i = 0; i < node->GetMaterialCount(); ++i) {
		n.material = node->GetMaterial(i)->GetName();
		break;
	}
	out.nodes.push_back(std::move(n));
	++ret;
	const int32_t self = (int32_t)out.nodes.size() - 1;
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		ret += ExtractNodes(out, node->GetChild(i), self);
	}
	return ret;
}

bool FBXLoader::Extract(const std::string& filename, bool triangulate, bool use_animation_names, CookedModel& out,
	std::function<void(float, const std::string&)> on_progress) {
	auto report = [&on_progress](float p, const char* stage) {
		if (on_progress != nullptr) { on_progress(p, stage); }
	};
	report(0.0f, "Reading FBX file...");
	if (!LoadScene(filename, triangulate)) {
		return false;
	}
	out = CookedModel{};
	out.triangulate = triangulate;
	out.use_animation_names = use_animation_names;
	CookedModel::StampOf(filename, out.source_size, out.source_time);

	FbxNode* root = scene->GetRootNode();
	report(0.15f, "Loading materials...");
	for (int i = 0; i < root->GetChildCount(); ++i) {
		ExtractMaterials(out, root->GetChild(i));
	}
	report(0.30f, "Loading meshes...");
	for (int i = 0; i < root->GetChildCount(); ++i) {
		ExtractMeshes(out, root->GetChild(i));
	}
	report(0.70f, "Loading collision shapes...");
	for (int i = 0; i < root->GetChildCount(); ++i) {
		ExtractShapes(out, root->GetChild(i));
	}
	report(0.80f, "Loading animations...");
	ExtractSkeletons(filename, out, root, use_animation_names);
	report(0.90f, "Building scene entities...");
	for (int i = 0; i < root->GetChildCount(); ++i) {
		ExtractNodes(out, root->GetChild(i), -1);
	}
	return true;
}

void FBXLoader::CalculateTangents(std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
	for (unsigned i = 0; i < indices.size() - 2; i += 3) {
		Vertex& v0 = vertices[indices[i + 0]];
		Vertex& v1 = vertices[indices[i + 1]];
		Vertex& v2 = vertices[indices[i + 2]];
		// Edges of the triangle : postion delta
		float3 edge1 = v1.Position - v0.Position;
		float3 edge2 = v2.Position - v0.Position;

		// UV delta
		float2 deltaUV1 = v1.UV - v0.UV;
		float2 deltaUV2 = v2.UV - v0.UV;

		float dev = (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
		if (dev != 0.0f) {
			float r = 1.0f / dev;

			float3 tangent = {};
			float3 bitangent = {};
			tangent.x = r * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
			tangent.y = r * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
			tangent.z = r * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

			bitangent.x = r * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
			bitangent.y = r * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
			bitangent.z = r * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
			v0.Tangent = v1.Tangent = v2.Tangent = tangent;
			v0.Bitangent = v1.Bitangent = v2.Bitangent = bitangent;
		}
	}
}

float  FBXLoader::GetMaterialProperty(const FbxSurfaceMaterial* pMaterial,
	const char* pPropertyName)
{
	float ret = 0.0;
	const FbxProperty lProperty = pMaterial->FindProperty(pPropertyName);
	if (lProperty.IsValid())
	{
		ret = (float)lProperty.Get<FbxDouble>();
	}
	return ret;
}

float4 FBXLoader::GetMaterialProperty(const FbxSurfaceMaterial* pMaterial,
	const char* pPropertyName,
	const char* pFactorPropertyName,
	std::string* pTextureName)
{
	FbxDouble3 ret(0, 0, 0);
	const FbxProperty lProperty = pMaterial->FindProperty(pPropertyName);
	const FbxProperty lFactorProperty = pMaterial->FindProperty(pFactorPropertyName);
	if (lProperty.IsValid())
	{
		ret = lProperty.Get<FbxDouble3>();
		if (lFactorProperty.IsValid()) {
			double lFactor = lFactorProperty.Get<FbxDouble>();
			if (lFactor != 1)
			{
				ret[0] *= lFactor;
				ret[1] *= lFactor;
				ret[2] *= lFactor;
			}
		}
	}

	if (lProperty.IsValid() && pTextureName != nullptr)
	{
		const int lTextureCount = lProperty.GetSrcObjectCount<FbxFileTexture>();
		if (lTextureCount)
		{
			const FbxFileTexture* lTexture = lProperty.GetSrcObject<FbxFileTexture>();
			if (lTexture)
			{
				*pTextureName = lTexture->GetFileName();
			}
		}
	}
	float4 ret2 = { static_cast<float>(ret[0]), static_cast<float>(ret[1]), static_cast<float>(ret[2]), static_cast<float>(1.0f) };
	return ret2;
}