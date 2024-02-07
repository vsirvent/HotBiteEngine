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

#include "Base.h"
#include <cmath>
#include <Core\Mesh.h>
#include <Core\SimpleShader.h>
#include <Core\SpinLock.h>
#include <Core\Utils.h>
#include <Core\Json.h>

namespace HotBite {
	namespace Engine {
		namespace Components {
						

			bool Material::LoadMultitexture(const std::string& json_str, const std::string& root_path, const Core::FlatMap<std::string, Core::MaterialData>& materials) {
				try
				{
					const nlohmann::json multi_textures_json = nlohmann::json::parse(json_str);
					multi_texture_count = multi_textures_json["count"];
					multi_texture_data.resize(multi_texture_count);
					multi_texture_mask.resize(multi_texture_count);
					multi_texture_operation.resize(multi_texture_count);
					multi_texture_uv_scales.resize(multi_texture_count);
					multi_texture_value.resize(multi_texture_count);

					if (multi_textures_json.contains("parallax_scale")) {
						multi_parallax_scale = multi_textures_json["parallax_scale"];
					}
					if (multi_textures_json.contains("tess_type")) {
						tessellation_type = multi_textures_json["tess_type"];
					}
					if (multi_textures_json.contains("tess_factor")) {
						tessellation_factor = multi_textures_json["tess_factor"];
					}
					if (multi_textures_json.contains("displacement_scale")) {
						displacement_scale = multi_textures_json["displacement_scale"];
					}
					for (auto& multi_texture : multi_textures_json["textures"]) {
						int layer = multi_texture["layer"];
						std::string mask_texture = (std::string)multi_texture["mask"];
						if (!mask_texture.empty()) {
							multi_texture_mask[layer] = Core::LoadTexture(root_path + "\\" + mask_texture);
						}
						else {
							multi_texture_mask[layer] = nullptr;
						}
						multi_texture_operation[layer] = multi_texture["op"];
						multi_texture_uv_scales[layer] = multi_texture["uv_scale"];
						multi_texture_value[layer] = multi_texture["value"];
						multi_texture_data[layer] = materials.Get(multi_texture["texture"]);
						if (multi_texture.contains("mask_noise") && multi_texture["mask_noise"] == 1) {
							multi_texture_operation[layer] |= TEXT_MASK_NOISE;
						}
						if (multi_texture.contains("uv_noise") && multi_texture["uv_noise"] == 1) {
							multi_texture_operation[layer] |= TEXT_UV_NOISE;
						}
					}
					for (uint32_t i = 0; i < multi_texture_count; ++i) {
						if (multi_texture_data[i] != nullptr) {
							if (multi_texture_data[i]->diffuse != nullptr) {
								multi_texture_operation[i] |= TEXT_DIFF;
							}
							if (multi_texture_data[i]->normal != nullptr) {
								multi_texture_operation[i] |= TEXT_NORM;
							}
							if (multi_texture_data[i]->spec != nullptr) {
								multi_texture_operation[i] |= TEXT_SPEC;
							}
							if (multi_texture_data[i]->ao != nullptr) {
								multi_texture_operation[i] |= TEXT_AO;
							}
							if (multi_texture_data[i]->arm != nullptr) {
								multi_texture_operation[i] |= TEXT_ARM;
							}
							if (multi_texture_data[i]->high != nullptr) {
								multi_texture_operation[i] |= TEXT_DISP;
							}
							if (multi_texture_mask[i] != nullptr) {
								multi_texture_operation[i] |= TEXT_MASK;
							}
						}
					}
				}
				catch (...) {
					printf("Material::LoadMultitexture: error loading multitexture");
					return false;
				}
				return true;
			}

			Mesh::Mesh() {
				end_animation_event.SetSender(this);
				end_animation_event.SetType(EVENT_ID_ANIMATION_END);
				skeleton_mutex = std::make_shared<std::recursive_mutex>();
			}

			Mesh::~Mesh() {
			}

			void Mesh::SetData(Core::MeshData* mesh) {
				data = mesh;
				joint_cpu_data.clear();
				joint_gpu_data.clear();
				if (data != nullptr && !data->skeletons.empty()) {
					current_animation.skeleton = data->skeletons[0];
					joint_gpu_data.resize(current_animation.skeleton->CpuData().size());
					joint_cpu_data.resize(current_animation.skeleton->CpuData().size());
					current_animation.id = 0;
				}
				index_count = data->indexCount;
				index_offset = data->indexOffset;
				vertex_offset = data->vertexOffset;
			}

			Core::MeshData* Mesh::GetData() {
				return data;
			}

			void Mesh::SetCoordinatorInfo(ECS::Entity e, ECS::Coordinator* c) {
				coordinator = c;
				entity = e;
				end_animation_event.SetEntity(e);
			}			
				
			void Mesh::SetAnimationDefaultTransitionTime(float time) {
				animation_default_change_time = time;
			}

			float Mesh::GetAnimationDefaultTransitionTime() const {
				return animation_default_change_time;
			}

			bool Mesh::SetAnimation(const std::string& name, bool loop, bool sync, float transition_time, float speed, bool force) {
				bool ret = false;
				if (current_animation.name != name || force) {
					for (auto& skl : data->skeletons) {
						std::vector<Core::JointCpuData>& cpu_data = skl->CpuData();
						std::unordered_map<int, std::string> animations;
						for (int i = 0; i < cpu_data.size(); ++i) {
							if (!cpu_data[i].animations.empty()) {
								for (int j = 0; j < cpu_data[i].animations.size(); ++j) {
									const Core::JointAnim& animation = cpu_data[i].animations[j];
									if (animation.name == name) {
										skeleton_mutex->lock();
										animation_change_current_time = 0.0f;
										previous_animation = current_animation;
										current_animation.t0 = current_animation.t1;
										current_animation.skeleton = skl;
										current_animation.id = j;
										current_animation.name = name;
										current_animation.loop = loop;
										current_animation.sync = sync;
										current_animation.key_frame = 0;
										current_animation.speed = speed;
										if (transition_time >= 0.0f) {
											animation_change_time = transition_time;
										}
										else {
											animation_change_time = animation_default_change_time;
										}
										ret = true;
										skeleton_mutex->unlock();
										goto end;
									}
								}
							}
						}
					}
				}
			end:
				return ret;
			}

			int Mesh::GetCurrentAnimationId() const {
				return current_animation.id;
			}
						
			std::string Mesh::GetCurrentAnimationName() const {
				return current_animation.name;
			}

			int Mesh::GetCurrentFrame() const {
				return current_animation.key_frame;
			}

			matrix Mesh::GetAnimationMatrix(int64_t elapsed_nsec, int64_t total_nsec, std::vector<Core::JointCpuData>* cpu_data, int joint_id, Animation& anim) {
				matrix ret = DirectX::XMMatrixIdentity();
				Core::JointAnim* animation = &((*cpu_data)[joint_id].animations[anim.id]);
				if (animation != nullptr && animation->key_frames.size() > 0) {

					//elapsed msec
					if (anim.speed > 0.0f) {
						anim.t1 = time_offset + total_nsec / 1000000;
						anim.t1 = (int64_t)((float)anim.t1 * anim.speed);
						if (anim.sync) {
							anim.t1 -= anim.t0;
						}
					}

					//key frame by fps, animation and elapsed time
					float new_keyf = ((float)anim.t1 * animation->fps) / 1000.0f;
					float max_frames = std::fmax((float)animation->key_frames.size() - 1.0f, 1.0f);
					if (anim.loop) {
						//loop the key frame
						new_keyf = std::fmax(fmod(new_keyf, max_frames), 0.0f);
					}
					else {
						new_keyf = std::fmax(std::clamp(new_keyf, 0.0f, max_frames - 1.0f), 0.0f);
					}
					//Get the integer key frame
					int k0 = (int)new_keyf;
					//Get the integer next key frame
					int k1 = (k0 + 1) % animation->key_frames.size();
					//Calculate weights for animation interpolation between keyframes
					float w1 = new_keyf - (float)k0;
					float w0 = 1.0f - w1;
					matrix m0 = XMLoadFloat4x4(&animation->key_frames[k0].transform);
					matrix m1 = XMLoadFloat4x4(&animation->key_frames[k1].transform);
					matrix bp = XMLoadFloat4x4(&data->skeletons[0]->CpuData()[joint_id].model_to_bindpose);

					ret = bp * (m0 * w0 + m1 * w1);
					if (!animation->key_frames.empty() && (k1 + 1) >= animation->key_frames.size() && coordinator != nullptr) {
						end_animation_event.SetParam(EVENT_PARAM_ANIMATION_ID, anim.id);
						end_animation_event.SetParam(EVENT_PARAM_ANIMATION_NAME, anim.name);
						coordinator->SendEvent(end_animation_event);						
					}
					if (anim.key_frame != k0) {
						anim.key_frame = k0;
						Core::AutoLock l(animation->lock);
						if (const auto it = animation->frame_events.find(k0); it != animation->frame_events.cend()) {
							ECS::Event frame_event(this, entity, EVENT_ID_ANIMATION_FRAME_EVENT);			
							frame_event.SetParam(EVENT_PARAM_ANIMATION_NAME, anim.name);
							frame_event.SetParam(EVENT_PARAM_ANIMATION_FRAME, it->first);
							frame_event.SetParam(EVENT_PARAM_ANIMATION_FRAME_ID, it->second.ids);
							coordinator->SendEvent(frame_event);
						}
					}
				}
				return ret;
			}

			void Mesh::Update(int64_t elapsed_nsec, int64_t total_nsec) {
				skeleton_mutex->lock();
				if (current_animation.skeleton != nullptr) {
					std::vector<Core::JointCpuData>* current_cpu_data = &current_animation.skeleton->CpuData();
					std::vector<Core::JointCpuData>* prev_cpu_data = nullptr;
					if (previous_animation.skeleton != nullptr) {
						prev_cpu_data = &previous_animation.skeleton->CpuData();
					}
					float w0 = animation_change_time > 0.0f?(animation_change_time - animation_change_current_time) / (animation_change_time):0.0f;
					float w1 = 1.0f - w0;
					if (prev_cpu_data == nullptr || prev_cpu_data->size() < current_cpu_data->size()) {
						animation_change_current_time = animation_change_time;
						w0 = 0.0f;
					}
					if (current_animation.id >= 0) {
						matrix m;
						for (int i = 0; i < current_cpu_data->size(); ++i) {							
							if (w0 > 0.0f) {								
								m = GetAnimationMatrix(elapsed_nsec, total_nsec, prev_cpu_data, i, previous_animation) * w0 +
									GetAnimationMatrix(elapsed_nsec, total_nsec, current_cpu_data, i, current_animation) * w1;		
							}
							else {
								m = GetAnimationMatrix(elapsed_nsec, total_nsec, current_cpu_data, i, current_animation);
							}
							DirectX::XMStoreFloat4x4(&joint_gpu_data[i].skinning_matrix, XMMatrixTranspose(m));
						}						
					}
					if (animation_change_current_time < animation_change_time) {
						animation_change_current_time += elapsed_nsec / 1000000;
					}
				}
				skeleton_mutex->unlock();
			}

			void Mesh::Prepare(Core::SimpleVertexShader* vs) {
				if (current_animation.skeleton != nullptr && current_animation.id >= 0 && joint_gpu_data.size() > 0) {
					skeleton_mutex->lock();
					Core::JointGpuData* data = joint_gpu_data.data();
					vs->SetData("joints", (void*)data, sizeof(float4x4) * (int)joint_gpu_data.size());
					vs->SetInt(Core::SimpleShaderKeys::NJOINTS, (int)joint_gpu_data.size());
					skeleton_mutex->unlock();
				}
				else {
					vs->SetInt(Core::SimpleShaderKeys::NJOINTS, 0);
				}
			}

			void Mesh::Unprepare(Core::SimpleVertexShader* vs) {
				vs->SetInt(Core::SimpleShaderKeys::NJOINTS, 0);
			}
		}
	}
}