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

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <fstream>
#include <Defines.h>
#include <Core/Interfaces.h>
#include <Core/Update.h>
#include <Core/Vertex.h>
#include <Core/SimpleShader.h>
#include <Core/BVH.h>

namespace HotBite {
	namespace Engine {
		namespace Core {

			struct Keyframe {
				int64_t id;
				float4x4 transform = {};				
			};

			struct FrameEvent {
				std::unordered_set<int> ids;
			};

			struct JointAnim {
				std::string name;
				float start;
				float end;
				float duration;
				bool loop = true;
				float fps = 30.0f;
				std::vector<Keyframe> key_frames;				
				spin_lock lock;
				std::map<int, FrameEvent> frame_events;
			};

			struct JointCpuData {				
				int joint_id = -1;
				int id = -1;
				int parent_id = -1;
				std::string name;
				std::vector<JointAnim> animations;
				//This is the globalBindposeInverseMatrix and is fixed
				float4x4 model_to_bindpose = {};
			};

			struct JointGpuData {
				float4x4 skinning_matrix = {};
			};

			struct Joint {
				JointCpuData cpu_data;
			};

			class Skeleton {
			private:
				std::map<int, Joint> joints_by_cpu_id;
				std::unordered_map<int, std::list<Joint*>> joint_tree;
				std::vector<JointCpuData> joint_cpu_data;
			
				void AddJoints(int& counter, int parent_id, const std::unordered_map<int, std::list<Joint*>>::iterator& node) {
					for (auto child_node : node->second) {
						child_node->cpu_data.parent_id = parent_id;
						child_node->cpu_data.id = counter++;
						joint_cpu_data.push_back(child_node->cpu_data);
						std::unordered_map<int, std::list<Joint*>>::iterator child_nodes = joint_tree.find(child_node->cpu_data.joint_id);
						if (child_nodes != joint_tree.end()) {
							AddJoints(counter, child_node->cpu_data.id, child_nodes);
						}
					}
				}

			public:
				void AddJoint(const Joint& joint, int parent) {
					joints_by_cpu_id[joint.cpu_data.joint_id] = joint;
					joint_tree[parent].push_back(&joints_by_cpu_id[joint.cpu_data.joint_id]);
				}

				bool AddAnimationFrameEvent(int frame, int id, const std::string& animation_name = "") {
					for (auto& data : joint_cpu_data) {
						for (auto& anim : data.animations) {
							if (animation_name.empty() || anim.name == animation_name) {
								AutoLock l(anim.lock);
								anim.frame_events[frame].ids.insert(id);
								return true;
							}
						}
					}
					return false;
				}

				bool RemoveAnimationFrameEvent(int frame, int id, const std::string& animation_name = "") {
					for (auto& data : joint_cpu_data) {
						for (auto& anim : data.animations) {
							if (animation_name.empty() || anim.name == animation_name) {
								AutoLock l(anim.lock);
								for (auto& event : anim.frame_events) {
									if (event.first == frame) {
										for (auto it = event.second.ids.begin(); it != event.second.ids.end(); ++it) {
											if (*it == id) {
												event.second.ids.erase(it);
												return true;
											}
										}
									}
								}
							}
						}
					}
					return false;
				}

				bool ClearAnimationFrameEvents(const std::string& animation_name = "") {
					for (auto& data : joint_cpu_data) {
						for (auto& anim : data.animations) {
							if (animation_name.empty() || anim.name == animation_name) {
								AutoLock l(anim.lock);
								anim.frame_events.clear();
								return true;
							}
						}
					}
					return false;
				}

				void Flush() {
					joint_cpu_data.clear();					
					int counter = 0;
					std::unordered_map<int, std::list<Joint*>>::iterator null_node = joint_tree.find(-1);
					if (null_node != joint_tree.end()) {
						for (std::list<Joint*>::iterator root_node = null_node->second.begin(); root_node != null_node->second.end(); ++root_node) {
							std::unordered_map<int, std::list<Joint*>>::iterator child_node = joint_tree.find((*root_node)->cpu_data.joint_id);
							(*root_node)->cpu_data.parent_id = -1;
							(*root_node)->cpu_data.id = counter++;
							joint_cpu_data.push_back((*root_node)->cpu_data);
							if (child_node != joint_tree.end()) {
								AddJoints(counter, (*root_node)->cpu_data.id, child_node);
							}
						}
					}
				}

				const Joint* GetJointByCpuId(int id) {
					const Joint* ret = nullptr;
					auto it = joints_by_cpu_id.find(id);
					if (it != joints_by_cpu_id.end()) {
						ret = &(it->second);
					}
					return ret;
				}

				std::vector<JointCpuData>& CpuData() {
					return joint_cpu_data;
				}

				std::unordered_map<int, std::string> GetAnimations();

				bool Save(const std::string& filename) const {
					std::ofstream file(filename, std::ios::binary);
					if (!file) {
						printf("Failed to open file %s for writing.\n", filename.c_str());
						return false;
					}

					// Write number of joints
					size_t num_joints = joint_cpu_data.size();
					file.write(reinterpret_cast<const char*>(&num_joints), sizeof(int));

					// Write joint CPU data
					file.write(reinterpret_cast<const char*>(&joint_cpu_data[0]), num_joints * sizeof(JointCpuData));

					// Write joint tree
					size_t num_nodes = joint_tree.size();
					file.write(reinterpret_cast<const char*>(&num_nodes), sizeof(int));

					for (const auto& node : joint_tree) {
						// Write joint ID
						int joint_id = node.first;
						file.write(reinterpret_cast<const char*>(&joint_id), sizeof(int));

						// Write number of children
						size_t num_children = node.second.size();
						file.write(reinterpret_cast<const char*>(&num_children), sizeof(int));

						// Write child IDs
						for (const auto& child : node.second) {
							int child_id = child->cpu_data.id;
							file.write(reinterpret_cast<const char*>(&child_id), sizeof(int));
						}
					}
					file.close();
					return true;
				}

				bool Load(const std::string& file_path) {
					std::ifstream input_file(file_path, std::ios::binary);
					if (!input_file) {
						printf("Failed to open file for reading: %s\n", file_path.c_str());
						return false;
					}

					// Load the joint data
					size_t joint_count;
					input_file.read(reinterpret_cast<char*>(&joint_count), sizeof(joint_count));
					for (int i = 0; i < joint_count; ++i) {
						JointCpuData joint_data;
						input_file.read(reinterpret_cast<char*>(&joint_data), sizeof(joint_data));

						Joint joint;
						joint.cpu_data = joint_data;
						joints_by_cpu_id[joint.cpu_data.joint_id] = joint;
					}

					// Load the joint tree
					size_t tree_size;
					input_file.read(reinterpret_cast<char*>(&tree_size), sizeof(tree_size));
					for (int i = 0; i < tree_size; ++i) {
						int parent_id;
						input_file.read(reinterpret_cast<char*>(&parent_id), sizeof(parent_id));

						int child_count;
						input_file.read(reinterpret_cast<char*>(&child_count), sizeof(child_count));
						for (int j = 0; j < child_count; ++j) {
							int child_id;
							input_file.read(reinterpret_cast<char*>(&child_id), sizeof(child_id));

							joint_tree[parent_id].push_back(&joints_by_cpu_id[child_id]);
						}
					}

					// Load the joint CPU data
					size_t cpu_data_size;
					input_file.read(reinterpret_cast<char*>(&cpu_data_size), sizeof(cpu_data_size));
					joint_cpu_data.resize(cpu_data_size);
					input_file.read(reinterpret_cast<char*>(joint_cpu_data.data()), cpu_data_size * sizeof(JointCpuData));
					return true;
				}
			};

			struct MeshData
			{
				MeshData();
				~MeshData();
				MeshData(const MeshData& other) {
					assert(!other.init && "MeshData can't be copied after init.");
					*this = other;
				}
				void Init(Core::VertexBuffer<Core::Vertex>* vb, const std::string& mesh_name, const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices, std::shared_ptr<Skeleton> skeleton);
				void Release();
				void LoadTextures();
				void AddSkeleton(std::shared_ptr<Skeleton> skl);
				std::unordered_map<int, std::string> GetAnimations();

				HotBite::Engine::Core::BVH bvh;
				float3 minDimensions = {};
				float3 maxDimensions = {};
				size_t indexOffset = 0;
				size_t vertexOffset = 0;
				size_t bvhOffset = 0;
				uint32_t indexCount = 0;
				uint32_t vertexCount = 0;
				std::string name;
				std::vector<Core::Vertex> vertices;
				std::vector<uint32_t> indices;
				std::vector<std::shared_ptr<Skeleton>> skeletons;
				ID3D11ShaderResourceView* normal_map = nullptr;
				bool init = false;
				std::string mesh_normal_texture;
				int tessellation_type = 0;
				float tessellation_factor = 0.0f;		
				float displacement_scale = 0.0f;
			};
		}
	}
}