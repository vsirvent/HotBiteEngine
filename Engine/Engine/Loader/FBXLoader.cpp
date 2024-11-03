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

int FBXLoader::LoadSkeletons(const std::string& filename, Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>& skeletons, FbxNode* node, bool use_animation_names) {
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
		LoadAnimations(skl, node, node->GetParent(), use_animation_names?"":name);
		skl->Flush();
		printf("Skeleton with %llu bones loaded\n", skl->CpuData().size());
		skeletons.Insert(name, skl);
	}
	else {
		//Load node childs
		for (int i = 0; i < node->GetChildCount(); ++i) {
			FbxNode* child_node = node->GetChild(i);
			LoadSkeletons(filename, skeletons, child_node, use_animation_names);
		}
	}
	return ret;
}

int FBXLoader::LoadMeshes(Core::FlatMap<std::string, Core::MeshData>& meshes, FbxNode* node, Core::VertexBuffer<Vertex>* vb) {
	int ret = 0;
	if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		std::string name = node->GetName();
		std::vector<unsigned int> indices;
		std::unordered_map<int, std::vector<int>> cloned_vertices;

		FbxMesh* fbxMesh = (FbxMesh*)node->GetNodeAttribute();
		FbxVector4* controlPoints = fbxMesh->GetControlPoints();
		std::vector<Vertex> vertices;

		bool smooth = true;
		FbxProperty p = node->FindProperty("smooth", false);
		if (p.IsValid())
		{
			smooth = (bool)p.Get<int>();
		}
		if (name.find(".NoSmooth") != std::string::npos) {
			smooth = false;
		}

		int vertex = 0;
		int polygonCount = fbxMesh->GetPolygonCount();
		for (int i = 0; i < polygonCount; i++)
		{
			int polygonSize = fbxMesh->GetPolygonSize(i);
			for (int j = 0; j < polygonSize; j++)
			{
				vertices.push_back({});
				int index = vertex++;
				int fbx_id = fbxMesh->GetPolygonVertex(i, j);
				cloned_vertices[fbx_id].push_back(index);
				indices.push_back(index);
				
				vertices[index].Position.x = (float)controlPoints[fbx_id].mData[0];
				vertices[index].Position.y = (float)controlPoints[fbx_id].mData[1];
				vertices[index].Position.z = (float)controlPoints[fbx_id].mData[2];

				FbxVector4 norm(0, 0, 0, 0);
				fbxMesh->GetPolygonVertexNormal(i, j, norm);
				vertices[index].Normal.x = (float)norm.mData[0];
				vertices[index].Normal.y = (float)norm.mData[1];
				vertices[index].Normal.z = (float)norm.mData[2];

				FbxVector2 uvCoord(0, 0);
				bool uvFlag = false;
				if (fbxMesh->GetPolygonVertexUV(i, j, "UVMap", uvCoord, uvFlag)) {
					vertices[index].UV.x = (float)uvCoord.mData[0];
					vertices[index].UV.y = 1.0f - (float)uvCoord.mData[1];
				}
				if (fbxMesh->GetPolygonVertexUV(i, j, "MeshUVMap", uvCoord, uvFlag)) {
					vertices[index].MeshUV.x = (float)uvCoord.mData[0];
					vertices[index].MeshUV.y = 1.0f - (float)uvCoord.mData[1];
				}
			}
		}
		
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

		
		if (smooth) {
			MixSimilarVertices(vertices, cloned_vertices);
		}

		printf("Loaded mesh %s\n", name.c_str());
		MeshData* mesh = meshes.Create(name);
		mesh->Init(vb, name, vertices, indices, skeleton);
		++ret;
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		LoadMeshes(meshes, child_node, vb);
	}
	return ret;
}

int FBXLoader::LoadMaterials(Core::FlatMap<std::string, Core::MaterialData>& materials, FbxNode* node) {
	int ret = 0;
	for (int i = 0; i < node->GetMaterialCount(); ++i) {
		FbxSurfaceMaterial* sm = node->GetMaterial(i);
		std::string name = sm->GetName();
		if (sm != nullptr && materials.Get(name) == nullptr) {			
			MaterialData* m = materials.Create(name);
			m->name = name;
			m->props.ambientColor = GetMaterialProperty(sm, FbxSurfaceMaterial::sAmbient, FbxSurfaceMaterial::sAmbientFactor, nullptr);
			m->props.diffuseColor = GetMaterialProperty(sm, FbxSurfaceMaterial::sDiffuse, FbxSurfaceMaterial::sDiffuseFactor, &(m->texture_names.diffuse_texname));
			m->props.specIntensity = 0.0f;
			printf("FBXLoader::Added material %s\n", name.c_str());
			++ret;
		}
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		LoadMaterials(materials, child_node);
	}
	return ret;
}

int FBXLoader::LoadShapes(Core::FlatMap<std::string, Core::ShapeData>& shapes, FbxNode* node) {	
	int ret = 0;
	if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		std::string name = node->GetName();
		Core::ShapeData* shape = shapes.Create(name);
		FbxAMatrix& matrix = node->EvaluateGlobalTransform();
		FbxVector4 s = matrix.GetS();
		float3 scale = { abs((float)s.mData[0] / (RATIO)), abs((float)s.mData[1] / (RATIO)), abs((float)s.mData[2] / (RATIO)) };
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
		if (shape->vertices.size() > 0) {
			// Create the polygon vertex array 
			reactphysics3d::TriangleVertexArray* triangleVertexArray = new reactphysics3d::TriangleVertexArray((uint32_t)shape->vertices.size(),
				shape->vertices.data(), (uint32_t)sizeof(float3),
				shape->normals.data(), (uint32_t)sizeof(float),
				(uint32_t)shape->indices.size() / 3, shape->indices.data(), (uint32_t)sizeof(unsigned int) * 3,
				reactphysics3d::TriangleVertexArray::VertexDataType::VERTEX_FLOAT_TYPE,
				reactphysics3d::TriangleVertexArray::NormalDataType::NORMAL_FLOAT_TYPE,
				reactphysics3d::TriangleVertexArray::IndexDataType::INDEX_INTEGER_TYPE);

			// Create the polyhedron mesh 
			reactphysics3d::TriangleMesh* triangleMesh = Core::physics_common.createTriangleMesh();
			triangleMesh->addSubpart(triangleVertexArray);
			shape->shape = Core::physics_common.createConcaveMeshShape(triangleMesh);
		}
		printf("Loaded shape %s\n", name.c_str());			
		++ret;		
	}
	//Load node childs
	for (int i = 0; i < node->GetChildCount(); ++i) {
		FbxNode* child_node = node->GetChild(i);
		LoadShapes(shapes, child_node);
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

std::unordered_set<Entity> 
FBXLoader::ProcessEntity(Core::FlatMap<std::string, Core::MeshData>& meshes,
						 Core::FlatMap<std::string, Core::MaterialData>& materials,
						 Core::FlatMap<std::string, Core::ShapeData>& shapes,
						 Coordinator* coordinator, FbxNode* node, Entity parent) {

	std::unordered_set<Entity> ret;
	if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
		return ret;
	}
	std::string name = node->GetName();
	Entity e = coordinator->GetEntityByName(name);
	if (e == INVALID_ENTITY_ID) {
		e = coordinator->CreateEntity(name);	
		ret.insert(e);

		coordinator->AddComponent<Components::Base>(e);
		coordinator->AddComponent<Components::Transform>(e);
		coordinator->AddComponent<Components::Bounds>(e);
		
		Components::Base& base = coordinator->GetComponent<Components::Base>(e);
		Components::Transform& transform = coordinator->GetComponent<Components::Transform>(e);
		base.name = name;
		base.id = e;
		if (parent != INVALID_ENTITY_ID) {
			base.parent = parent;
		}
		FbxAMatrix& m = node->EvaluateGlobalTransform();
		FbxVector4 t = m.GetT();
		FbxVector4 s = m.GetS();
		FbxQuaternion r = m.GetQ();

		transform.position.x = (float)t.mData[0] / (RATIO);
		transform.position.y = (float)t.mData[1] / (RATIO);
		transform.position.z = (float)t.mData[2] / (RATIO);
		transform.scale.x = abs((float)s.mData[0] / (RATIO));
		transform.scale.y = abs((float)s.mData[1] / (RATIO));
		transform.scale.z = abs((float)s.mData[2] / (RATIO));
		transform.rotation.x = (float)r.mData[0];
		transform.rotation.y = (float)r.mData[1];
		transform.rotation.z = (float)r.mData[2];
		transform.rotation.w = (float)r.mData[3];
		transform.initial_rotation = transform.rotation;
				
		if (parent != INVALID_ENTITY_ID) {
			base.parent_position = true;
			base.parent_rotation = false;
		}
		matrix trans = XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);
		matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform.rotation));
		matrix scle = XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z);
		transform.world_xmmatrix = scle * rot * trans;

		XMStoreFloat4x4(&transform.world_matrix, XMMatrixTranspose(transform.world_xmmatrix));
		XMStoreFloat4x4(&transform.world_inv_matrix, XMMatrixTranspose(XMMatrixInverse(nullptr, transform.world_xmmatrix)));

		transform.prev_world_matrix = transform.world_matrix;

		if (node->GetNodeAttribute())
		{
			auto type = node->GetNodeAttribute()->GetAttributeType();
			switch (type) {
			case FbxNodeAttribute::eLight: {
				ProcessLight(coordinator, e, node);
			}break;
			case FbxNodeAttribute::eCamera: {
				ProcessCamera(coordinator, e, node);
			}break;
			case FbxNodeAttribute::eMesh: {
				Components::Mesh mesh;
				mesh.SetData(meshes.Get(name));
				assert(mesh.GetData() != nullptr && "Mesh not found.");
				coordinator->AddComponent(e, std::move(mesh));
				coordinator->AddComponent(e, Components::Lighted{});

				Bounds& bounds = coordinator->GetComponent<Bounds>(e);
				Mesh& m = coordinator->GetComponent<Mesh>(e);

				auto minV = m.GetData()->minDimensions;
				auto maxV = m.GetData()->maxDimensions;

				float3 center = float3((maxV.x + minV.x) / 2.0f, (maxV.y + minV.y) / 2.0f, (maxV.z + minV.z) / 2.0f);
				float3 extends = float3(abs(maxV.x - minV.x) / 2.0f, abs(maxV.y - minV.y) / 2.0f, abs(maxV.z - minV.z) / 2.0f);
				bounds.local_box.Extents = extends;
				bounds.local_box.Center = center;
				bounds.final_box = bounds.local_box;

				matrix trans = XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);
				matrix rot = XMMatrixRotationQuaternion(XMLoadFloat4(&transform.rotation));
				matrix scle = XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z);
				transform.world_xmmatrix = scle * rot * trans;

				vector4d extents = XMLoadFloat3(&bounds.final_box.Extents);
				extents = XMVector4Transform(extents, transform.world_xmmatrix);
				bounds.local_box.Transform(bounds.final_box, transform.world_xmmatrix);

				BoundingOrientedBox local_oriented;
				local_oriented.Center = bounds.local_box.Center;
				local_oriented.Extents = bounds.local_box.Extents;
				local_oriented.Transform(bounds.bounding_box, transform.world_xmmatrix);
				
			}break;
			case FbxNodeAttribute::eSkeleton: {
				//Skeletons are loaded as part of the mesh
			}break;
			case FbxNodeAttribute::eNull: {
				//Empty node
			}
			default:
				printf("FBXLoader::ProcessEntity: Node %s, type %d has no components\n", name.c_str(), (int)type);
				break;
			}
		}
		for (int i = 0; i < node->GetMaterialCount(); ++i) {
			FbxSurfaceMaterial* sm = node->GetMaterial(i);
			std::string name = sm->GetName();
			MaterialData* m = materials.Get(name);
			assert(m != nullptr && "Unknown material");
			Components::Material material;
			material.data = m;
			coordinator->AddComponent(e, std::move(material));
			//We can only add one material component, even if the FBX file
			//includes multiple materials to the entity
			break;
		}
		coordinator->NotifySignatureChange(e);
		printf("Entity %s created with ID %d, Parent %d\n", name.c_str(), e, base.parent);
		//Load node childs
		for (int i = 0; i < node->GetChildCount(); ++i) {
			FbxNode* child_node = node->GetChild(i);
			for (const auto& ec : ProcessEntity(meshes, materials, shapes, coordinator, child_node, e)) {
				ret.insert(ec);
			}
		}
	}
	else {
		printf("Entity %s already exists.", name.c_str());
	}	
	return ret;
}

bool FBXLoader::ProcessCamera(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node) {
	bool ret = true;
	coordinator->AddComponent(e, Components::Camera{});
	return ret;
}

bool FBXLoader::ProcessShape(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node) {
	bool ret = true;
	return ret;
}

bool FBXLoader::ProcessMesh(ECS::Coordinator* coordinator, ECS::Entity e, FbxNode* node) {
	bool ret = true;
	return ret;
}


bool FBXLoader::ProcessLight(Coordinator * coordinator, Entity entity, FbxNode * node) {
	bool ret = false;
	if (node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLight)
	{
		FbxLight* fl = (FbxLight*)node->GetNodeAttribute();
		FbxLight::EType type = fl->LightType.Get();
		float intensity = (float)fl->Intensity / (RATIO);
		Components::Transform& t = coordinator->GetComponent<Components::Transform>(entity);

		switch (type) {
		case FbxLight::EType::eDirectional: {
			
			coordinator->AddComponent(entity, Components::DirectionalLight{});
			Components::DirectionalLight& dl = coordinator->GetComponent<Components::DirectionalLight>(entity);
			dl.GetData().color = { (float)fl->Color.Get()[0],
							   (float)fl->Color.Get()[1],
							   (float)fl->Color.Get()[2] };
			vector4d dir = XMVectorSet(t.position.x, t.position.y, t.position.z, 0.0f);
			XMVECTOR rot = { t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w };
			dir = XMVector3Rotate(dir, rot);
			XMStoreFloat3(&dl.GetData().direction, dir);
			dl.Init(dl.GetData().color, dl.GetData().direction, true, 1, 0.0f);

			ret = true;
		} break;
		case FbxLight::EType::eArea: {
			coordinator->AddComponent(entity, Components::AmbientLight{});
			Components::AmbientLight& al = coordinator->GetComponent<Components::AmbientLight>(entity);
			al.GetData().colorUp = { (float)fl->Color.Get()[0],
							   (float)fl->Color.Get()[1],
							   (float)fl->Color.Get()[2] };
			al.GetData().colorDown = { (float)fl->Color.Get()[0],
							   (float)fl->Color.Get()[1],
							   (float)fl->Color.Get()[2] };
			
			ret = true;
		} break;
		case FbxLight::EType::ePoint: {
			coordinator->AddComponent(entity, Components::PointLight{});
			Components::PointLight& pl = coordinator->GetComponent<Components::PointLight>(entity);
			pl.Init({ (float)fl->Color.Get()[0],
					  (float)fl->Color.Get()[1],
					  (float)fl->Color.Get()[2] },				
				(float)intensity,
				true, 1, 0.0f);
			ret = true;
		} break;
		default: {
			printf("Unknown light type %d\n", (int)type);
			ret = false;
		} break;
		}
	}
	return ret;
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

void FBXLoader::MixSimilarVertices(std::vector<Vertex>& vertices, const std::unordered_map<int, std::vector<int>>& cloned_vertices) {

	for (const auto& cv : cloned_vertices) {
		Vertex mixed_vertex = {};
		for (const auto& id : cv.second) {
			mixed_vertex.Normal.x += vertices[id].Normal.x;
			mixed_vertex.Normal.y += vertices[id].Normal.y;
			mixed_vertex.Normal.z += vertices[id].Normal.z;
			mixed_vertex.Tangent.x += vertices[id].Tangent.x;
			mixed_vertex.Tangent.y += vertices[id].Tangent.y;
			mixed_vertex.Tangent.z += vertices[id].Tangent.z;
			mixed_vertex.Bitangent.x += vertices[id].Bitangent.x;
			mixed_vertex.Bitangent.y += vertices[id].Bitangent.y;
			mixed_vertex.Bitangent.z += vertices[id].Bitangent.z;
		}
		mixed_vertex.Normal = UNIT_F3(mixed_vertex.Normal);
		mixed_vertex.Tangent = UNIT_F3(mixed_vertex.Tangent);
		mixed_vertex.Bitangent = UNIT_F3(mixed_vertex.Bitangent);

		for (const auto& id : cv.second) {
			vertices[id].Normal = mixed_vertex.Normal;
			vertices[id].Tangent = mixed_vertex.Tangent;
			vertices[id].Bitangent = mixed_vertex.Bitangent;
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