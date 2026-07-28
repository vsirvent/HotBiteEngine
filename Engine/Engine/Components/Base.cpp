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

// Must come before anything that reaches Windows.h: this file includes World.h
// (for the asset collections Material/Mesh deserialization resolves names
// against), which pulls in reactphysics3d, whose headers use
// std::numeric_limits<T>::max()/min() and break if the Windows min/max macros are
// already defined when they are first parsed. Tools/SceneEditor/SceneEditor.h and
// Tools/HotBiteTool/Tools.h follow the same include order for the same reason.
#include <Core/PhysicsCommon.h>

#include "Base.h"
#include <cmath>
#include <Core\Mesh.h>
#include <Core\SimpleShader.h>
#include <Core\SpinLock.h>
#include <Core\Utils.h>
#include <Core\Json.h>
//Material/Mesh serialize as asset *names*, so resolving them needs the world's
//material/mesh collections and its templates coordinator.
#include <World.h>

namespace HotBite {
	namespace Engine {
		namespace Components {
						

			bool MultiMaterial::LoadMultitexture(const std::string& json_str, const std::string& root_path, const Core::FlatMap<std::string, Core::MaterialData>& materials) {
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
				//A pose from the mesh that was here before is not a previous pose of this one;
				//Prepare reads a size mismatch as "no history" and skins both passes with the
				//current joints until the next frame latches a real one.
				prev_joint_gpu_data.clear();
				joint_pose_scratch.clear();
				{
					//A new mesh means new joints; anything socketed to the old ones reads
					//"no pose" until Update has filled the new set.
					Core::AutoLock pose_lock(joint_pose_lock);
					joint_pose_data.clear();
				}
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
				
			void Mesh::StopAnimation() {
				skeleton_mutex->lock();
				//id < 0 is what Prepare() reads as "no skinning for this mesh", so the
				//joints stop being uploaded and the mesh draws in its bind pose. The
				//skeleton pointer stays: it is the mesh's, not the animation's.
				previous_animation = current_animation;
				current_animation.name.clear();
				current_animation.id = -1;
				current_animation.key_frame = -1;
				animation_change_current_time = animation_change_time;
				skeleton_mutex->unlock();
			}

			void Mesh::SetAnimationDefaultTransitionTime(float time) {
				animation_default_change_time = time;
			}

			float Mesh::GetAnimationDefaultTransitionTime() const {
				return animation_default_change_time;
			}

			std::string Mesh::ResolveClip(const std::string& name) const {
				auto it = clips.find(name);
				return (it != clips.end() && !it->second.empty()) ? it->second : name;
			}

			bool Mesh::SetAnimation(const std::string& name, bool loop, bool sync, float transition_time, float speed, bool force) {
				bool ret = false;
				//What is searched for is the clip; what is remembered is the name the
				//caller used. An entity asked for "walk" reports "walk" - that is the
				//name its authoring data holds, and writing the clip back instead would
				//quietly rewrite a template's vocabulary into the file names it was
				//built to hide.
				const std::string clip = ResolveClip(name);
				if (current_animation.name != name || force) {
					for (auto& skl : data->skeletons) {
						std::vector<Core::JointCpuData>& cpu_data = skl->CpuData();
						std::unordered_map<int, std::string> animations;
						for (int i = 0; i < cpu_data.size(); ++i) {
							if (!cpu_data[i].animations.empty()) {
								for (int j = 0; j < cpu_data[i].animations.size(); ++j) {
									const Core::JointAnim& animation = cpu_data[i].animations[j];
									if (animation.name == clip) {
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

			bool Mesh::GetLocalBox(box& out) const {
				if (data == nullptr) {
					return false;
				}
				//Read without skeleton_mutex on purpose. The caller (StaticMeshSystem) runs
				//holding physics_mutex, and Mesh::Update sends events from *inside* that
				//lock, so taking it here would invert the order against any listener that
				//touches physics. The race it leaves is benign: the skeleton is owned by the
				//MeshData and outlives any animation change, so an unlucky read gets the
				//box of the clip being switched away from, for one frame.
				if (current_animation.skeleton != nullptr) {
					const box* animated = data->GetAnimationBox(current_animation.skeleton.get(),
						current_animation.id);
					if (animated != nullptr) {
						out = *animated;
						return true;
					}
				}
				//Nothing is skinning these vertices - no skeleton, no animation
				//(StopAnimation), or a clip with no keyframes - so the shader draws them
				//where they are stored, which is what these measure.
				const float3& lo = data->minDimensions;
				const float3& hi = data->maxDimensions;
				out.Center = { (hi.x + lo.x) * 0.5f, (hi.y + lo.y) * 0.5f, (hi.z + lo.z) * 0.5f };
				out.Extents = { fabsf(hi.x - lo.x) * 0.5f, fabsf(hi.y - lo.y) * 0.5f,
					fabsf(hi.z - lo.z) * 0.5f };
				return true;
			}

			int Mesh::GetCurrentFrame() const {
				return current_animation.key_frame;
			}

			int Mesh::FindJoint(const std::string& name) const {
				if (name.empty() || current_animation.skeleton == nullptr) {
					return -1;
				}
				const std::vector<Core::JointCpuData>& joints = current_animation.skeleton->CpuData();
				for (int i = 0; i < (int)joints.size(); ++i) {
					if (joints[i].name == name) {
						return i;
					}
				}
				return -1;
			}

			std::vector<std::string> Mesh::GetJointNames() const {
				std::vector<std::string> names;
				if (current_animation.skeleton == nullptr) {
					return names;
				}
				const std::vector<Core::JointCpuData>& joints = current_animation.skeleton->CpuData();
				names.reserve(joints.size());
				for (const Core::JointCpuData& joint : joints) {
					names.push_back(joint.name);
				}
				return names;
			}

			void Mesh::TrackJoints() {
				//Just the flag: Update does the sizing, under the pose lock. Turning
				//tracking on therefore costs the first reader one frame of "no pose yet",
				//which is why GetJointPose's false means "compose without the socket"
				//rather than "the joint is at the origin".
				track_joints = true;
			}

			bool Mesh::GetJointPose(int index, matrix& out) const {
				if (index < 0 || current_animation.id < 0) {
					return false;
				}
				Core::AutoLock lock(joint_pose_lock);
				if (index >= (int)joint_pose_data.size()) {
					return false;
				}
				out = joint_pose_data[index];
				return true;
			}

			matrix Mesh::GetAnimationMatrix(int64_t elapsed_nsec, int64_t total_nsec, std::vector<Core::JointCpuData>* cpu_data, int joint_id, Animation& anim, matrix* pose) {
				matrix ret = DirectX::XMMatrixIdentity();
				//`pose` is the same blend without the inverse bind pose: where the joint
				//*is*, rather than how it moves a vertex. It is handed out here because
				//this is the only place the blend exists; recomputing it outside would
				//mean interpolating the keyframes a second time.
				if (pose != nullptr) {
					*pose = ret;
				}
				//An Animation that is not playing carries id -1 (StopAnimation, or one
				//that never resolved a name), and a joint of another skeleton need not
				//have as many animations as that id assumes. Either way there is no
				//matrix to build, and identity is the bind pose - which is exactly what
				//a mesh with no animation must show. Indexing anyway is how "(none)"
				//followed by any animation crashed: the transition blend reads the
				//stopped animation as the pose it is coming from.
				if (joint_id < 0 || joint_id >= (int)cpu_data->size() || anim.id < 0 ||
					anim.id >= (int)(*cpu_data)[joint_id].animations.size()) {
					return ret;
				}
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

					const matrix joint_pose = m0 * w0 + m1 * w1;
					if (pose != nullptr) {
						*pose = joint_pose;
					}
					ret = bp * joint_pose;
					if (!animation->key_frames.empty() && (k1 + 1) >= animation->key_frames.size() && coordinator != nullptr) {
						end_animation_event.SetParam(EVENT_PARAM_ANIMATION_ID, anim.id);
						end_animation_event.SetParam(EVENT_PARAM_ANIMATION_NAME, anim.name);
						coordinator->SendEvent(end_animation_event);						
					}
					if (anim.key_frame != k0) {
						anim.key_frame = k0;
						Core::AutoLock l(animation->lock);
						//Null coordinator is a Mesh that belongs to no scene - the Scene
						//Editor's model preview drives one of its own so that previewing
						//an animation leaves no state on the template being cloned. The
						//end-of-animation send above already guards this; this one did
						//not, so any animation carrying a frame event crashed the moment
						//it reached that frame.
						if (const auto it = animation->frame_events.find(k0);
							it != animation->frame_events.cend() && coordinator != nullptr) {
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
					//Nothing to blend from when the previous animation is not one: coming
					//out of StopAnimation the mesh was in its bind pose, so the new
					//animation starts at full weight instead of fading in from a pose
					//that does not exist.
					if (prev_cpu_data == nullptr || previous_animation.id < 0 ||
						prev_cpu_data->size() < current_cpu_data->size()) {
						animation_change_current_time = animation_change_time;
						w0 = 0.0f;
					}
					if (current_animation.id >= 0) {
						if (track_joints && joint_pose_scratch.size() != current_cpu_data->size()) {
							//Sized against the skeleton actually playing: a clip from another
							//set has its own joint count, and the socket indices are re-derived
							//from it.
							joint_pose_scratch.assign(current_cpu_data->size(), DirectX::XMMatrixIdentity());
						}
						matrix m;
						matrix pose_prev;
						matrix pose_cur;
						for (int i = 0; i < current_cpu_data->size(); ++i) {
							if (w0 > 0.0f) {
								m = GetAnimationMatrix(elapsed_nsec, total_nsec, prev_cpu_data, i, previous_animation, track_joints ? &pose_prev : nullptr) * w0 +
									GetAnimationMatrix(elapsed_nsec, total_nsec, current_cpu_data, i, current_animation, track_joints ? &pose_cur : nullptr) * w1;
								if (track_joints) {
									joint_pose_scratch[i] = pose_prev * w0 + pose_cur * w1;
								}
							}
							else {
								m = GetAnimationMatrix(elapsed_nsec, total_nsec, current_cpu_data, i, current_animation, track_joints ? &pose_cur : nullptr);
								if (track_joints) {
									joint_pose_scratch[i] = pose_cur;
								}
							}
							DirectX::XMStoreFloat4x4(&joint_gpu_data[i].skinning_matrix, XMMatrixTranspose(m));
						}
						if (track_joints) {
							//Published in one step, after the last animation event has been
							//sent: nothing but this swap and GetJointPose's copy ever runs
							//under the pose lock.
							Core::AutoLock pose_lock(joint_pose_lock);
							joint_pose_data.swap(joint_pose_scratch);
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
					//Only the main render VS declares prev_joints - the depth, shadow and
					//preview ones neither write a position map nor care where the pose was, and
					//SetData is a no-op for a name they do not have. Falling back to the current
					//pose when there is no previous one (first frame, or a skeleton that just
					//changed size) reports "did not move", which is the right answer for a
					//frame with nothing to compare against.
					if (prev_joint_gpu_data.size() == joint_gpu_data.size()) {
						vs->SetData("prev_joints", (void*)prev_joint_gpu_data.data(),
							sizeof(float4x4) * (int)prev_joint_gpu_data.size());
					}
					else {
						vs->SetData("prev_joints", (void*)data, sizeof(float4x4) * (int)joint_gpu_data.size());
					}
					vs->SetInt(Core::SimpleShaderKeys::NJOINTS, (int)joint_gpu_data.size());
					skeleton_mutex->unlock();
				}
				else {
					vs->SetInt(Core::SimpleShaderKeys::NJOINTS, 0);
				}
			}

			void Mesh::LatchPrevJoints() {
				if (joint_gpu_data.empty()) {
					prev_joint_gpu_data.clear();
					return;
				}
				//Under the skeleton lock: Mesh::Update writes joint_gpu_data from the
				//background thread and resizes it whenever the skeleton changes, so an
				//unlocked copy can read a vector that is being reallocated.
				skeleton_mutex->lock();
				prev_joint_gpu_data = joint_gpu_data;
				skeleton_mutex->unlock();
			}

			void Mesh::Unprepare(Core::SimpleVertexShader* vs) {
				vs->SetInt(Core::SimpleShaderKeys::NJOINTS, 0);
			}

			// ---------------------------------------------------------------------------
			// Serialization. See ECS/Serialization.h for the contract: authoring state
			// only, and every key optional so a block applies as a delta.
			// ---------------------------------------------------------------------------

			using nlohmann::json;
			using namespace HotBite::Engine::ECS::JsonUtil;

			json Base::ToJson(const ECS::SerializeContext& ctx) const {
				json j;
				j["visible"] = visible;
				j["scene_visible"] = scene_visible;
				j["cast_shadow"] = cast_shadow;
				j["draw_depth"] = draw_depth;
				j["is_static"] = is_static;
				j["draw_method"] = (draw_method == eDrawMethod::DRAW_ALWAYS) ? "always" : "screen";
				j["pass"] = pass;
				//Parents round-trip by name; ids are recycled between sessions. The name
				//lives on the parent's own Base, which is where it is read from.
				if (parent != ECS::INVALID_ENTITY_ID && ctx.coordinator != nullptr &&
					ctx.coordinator->ContainsComponent<Base>(parent)) {
					const std::string& parent_name = ctx.coordinator->GetConstComponent<Base>(parent).name;
					if (!parent_name.empty()) {
						j["parent"] = parent_name;
						j["parent_position"] = parent_position;
						j["parent_rotation"] = parent_rotation;
						if (!parent_bone.empty()) {
							j["parent_bone"] = parent_bone;
						}
					}
				}
				return j;
			}

			void Base::FromJson(const json& j, const ECS::SerializeContext& ctx) {
				visible = j.value("visible", visible);
				scene_visible = j.value("scene_visible", scene_visible);
				cast_shadow = j.value("cast_shadow", cast_shadow);
				draw_depth = j.value("draw_depth", draw_depth);
				is_static = j.value("is_static", is_static);
				pass = j.value("pass", pass);
				parent_position = j.value("parent_position", parent_position);
				parent_rotation = j.value("parent_rotation", parent_rotation);
				if (j.contains("parent_bone") && j["parent_bone"].is_string()) {
					parent_bone = j["parent_bone"];
					parent_joint = UNRESOLVED_JOINT;
				}
				if (j.contains("draw_method") && j["draw_method"].is_string()) {
					draw_method = (j["draw_method"] == "always") ? eDrawMethod::DRAW_ALWAYS
						: eDrawMethod::DRAW_SCREEN;
				}
				if (j.contains("parent") && j["parent"].is_string() && ctx.coordinator != nullptr) {
					const std::string parent_name = j["parent"];
					if (!parent_name.empty()) {
						//A parent named but not yet loaded leaves the link unset rather than
						//pointing at INVALID_ENTITY_ID, so a later pass can still fix it up.
						ECS::Entity pe = ctx.coordinator->GetEntityByName(parent_name);
						if (pe != ECS::INVALID_ENTITY_ID) {
							parent = pe;
							parent_joint = UNRESOLVED_JOINT;
						}
						else {
							printf("Base::FromJson: unknown parent entity '%s'.\n", parent_name.c_str());
						}
					}
				}
			}

			json Transform::ToJson(const ECS::SerializeContext& ctx) const {
				json j;
				j["position"] = FromFloat3(position);
				j["rotation"] = FromFloat4(rotation);
				j["scale"] = FromFloat3(scale);
				return j;
			}

			void Transform::FromJson(const json& j, const ECS::SerializeContext& ctx) {
				ToFloat3(j, "position", position);
				ToFloat4(j, "rotation", rotation);
				ToFloat3(j, "scale", scale);
				//Any of the three invalidates the cached world matrices.
				dirty = true;
			}

			json Bounds::ToJson(const ECS::SerializeContext& ctx) const {
				json j;
				j["center"] = json{ {"x", local_box.Center.x}, {"y", local_box.Center.y}, {"z", local_box.Center.z} };
				j["extents"] = json{ {"x", local_box.Extents.x}, {"y", local_box.Extents.y}, {"z", local_box.Extents.z} };
				return j;
			}

			void Bounds::FromJson(const json& j, const ECS::SerializeContext& ctx) {
				//box uses XMFLOAT3, not the 16-byte-aligned float3, so these are read
				//field by field rather than through the float3 helpers.
				if (j.contains("center") && j["center"].is_object()) {
					const json& c = j["center"];
					local_box.Center.x = c.value("x", local_box.Center.x);
					local_box.Center.y = c.value("y", local_box.Center.y);
					local_box.Center.z = c.value("z", local_box.Center.z);
				}
				if (j.contains("extents") && j["extents"].is_object()) {
					const json& e = j["extents"];
					local_box.Extents.x = e.value("x", local_box.Extents.x);
					local_box.Extents.y = e.value("y", local_box.Extents.y);
					local_box.Extents.z = e.value("z", local_box.Extents.z);
				}
				//A Bounds added from scratch has zero extents, which culls the entity
				//away and gives any collider built from it a degenerate shape. Derive
				//it from the entity's own mesh, which is what the FBX loader would have
				//done, and fall back to a unit box when there is no mesh to measure.
				if (local_box.Extents.x == 0.0f && local_box.Extents.y == 0.0f &&
					local_box.Extents.z == 0.0f) {
					//Measured through the mesh rather than off its stored dimensions, so a
					//skinned entity gets the box of the animation it plays and not of the
					//bind pose it is stored in (Mesh::GetLocalBox).
					bool measured = false;
					if (ctx.coordinator != nullptr && ctx.entity != ECS::INVALID_ENTITY_ID &&
						ctx.coordinator->ContainsComponent<Mesh>(ctx.entity)) {
						measured = ctx.coordinator->GetComponent<Mesh>(ctx.entity).GetLocalBox(local_box);
					}
					if (!measured) {
						local_box.Center = { 0.0f, 0.0f, 0.0f };
						local_box.Extents = { 0.5f, 0.5f, 0.5f };
					}
					//The world-space boxes are recomputed from this by the transform
					//pass; seed them so the entity is not culled on its very first frame.
					final_box = local_box;
					bounding_box.Center = local_box.Center;
					bounding_box.Extents = local_box.Extents;
				}
			}

			//The entity in the templates coordinator named by a "template" key, or
			//INVALID_ENTITY_ID. Mesh and Material both accept the key, together
			//reproducing what the old top-level "template" did (adopt a template's mesh
			//*and* material) while letting a level adopt just one of the two.
			static ECS::Entity ResolveTemplateEntity(const json& j, const ECS::SerializeContext& ctx) {
				if (!j.contains("template") || !j["template"].is_string() || ctx.world == nullptr) {
					return ECS::INVALID_ENTITY_ID;
				}
				const std::string template_name = j["template"];
				if (template_name.empty()) {
					return ECS::INVALID_ENTITY_ID;
				}
				ECS::Coordinator* tc = ctx.world->GetTemplatesCoordinator();
				if (tc == nullptr) {
					return ECS::INVALID_ENTITY_ID;
				}
				//An authored template is registered under a prefixed entity name so it
				//cannot collide with an FBX node (World::TEMPLATE_ENTITY_PREFIX), and it
				//is checked first: it is the thing a user deliberately created under
				//that name, where a bare FBX node just happens to be called that.
				ECS::Entity te = tc->GetEntityByName(World::TEMPLATE_ENTITY_PREFIX + template_name);
				if (te == ECS::INVALID_ENTITY_ID) {
					te = tc->GetEntityByName(template_name);
				}
				if (te == ECS::INVALID_ENTITY_ID) {
					printf("Component::FromJson: unknown template entity '%s'.\n", template_name.c_str());
				}
				return te;
			}

			json Material::ToJson(const ECS::SerializeContext& ctx) const {
				json j;
				if (data != nullptr) {
					j["name"] = data->name;
				}
				return j;
			}

			void Material::FromJson(const json& j, const ECS::SerializeContext& ctx) {
				if (ctx.world == nullptr) {
					return;
				}
				ECS::Entity te = ResolveTemplateEntity(j, ctx);
				if (te != ECS::INVALID_ENTITY_ID) {
					ECS::Coordinator* tc = ctx.world->GetTemplatesCoordinator();
					if (tc->ContainsComponent<Material>(te)) {
						data = tc->GetConstComponent<Material>(te).data;
					}
				}
				if (j.contains("name") && j["name"].is_string()) {
					const std::string material_name = j["name"];
					//An unknown material leaves the current one in place: rendering with
					//the FBX-authored material is a far better failure than a null deref
					//in the render system.
					Core::MaterialData* found = ctx.world->GetMaterials().Get(material_name);
					if (found == nullptr && material_name == World::DEFAULT_MATERIAL_NAME) {
						//The editor's stand-in material is created on demand, so a scene
						//that references it is simply the first thing asking for it.
						found = ctx.world->GetDefaultMaterial();
					}
					if (found != nullptr) {
						data = found;
					}
					else {
						printf("Material::FromJson: unknown material '%s'.\n", material_name.c_str());
					}
				}
				if (j.contains("multi_texture")) {
					const json& mt = j["multi_texture"];
					multi_material.LoadMultitexture(mt.dump(), ctx.world->GetAssetsPath(),
						ctx.world->GetMaterials());
				}
				//Nothing named anything and no material to keep: this is a Material
				//added from scratch, so give it the default rather than leaving a null
				//pointer for the render system to trip over.
				if (data == nullptr) {
					data = ctx.world->GetDefaultMaterial();
				}
			}

			json Mesh::ToJson(const ECS::SerializeContext& ctx) const {
				json j;
				if (data != nullptr) {
					j["name"] = data->name;
					//Always written, never only when it differs from what the import
					//decided. Every FromJson reads a missing key as "leave alone", so a
					//ToJson that omits a field cannot be replayed to restore it - and
					//replaying an earlier ToJson is exactly what undo does
					//(ComponentOps::RecordEdit). Omitting this one left "unsmooth the
					//ball" undoable in name only: the step popped off the stack and the
					//ball stayed faceted.
					j["smooth"] = data->smooth;
					//The animation sets attached to this mesh, under the names they were
					//loaded with. The MeshData holds them as unnamed shared pointers, so
					//the names have to be recovered from the world's skeleton collection
					//- without them, an attachment could be made but never written back.
					if (ctx.world != nullptr && !data->skeletons.empty()) {
						json skeletons = json::array();
						auto& named = ctx.world->GetSkeletons();
						for (const std::string& name : named.Keys()) {
							std::shared_ptr<Core::Skeleton>* skl = named.Get(name);
							if (skl == nullptr) {
								continue;
							}
							for (const auto& attached : data->skeletons) {
								if (attached == *skl) {
									skeletons.push_back(name);
									break;
								}
							}
						}
						if (!skeletons.empty()) {
							j["skeletons"] = skeletons;
						}
					}
				}
				if (!clips.empty()) {
					json library = json::object();
					for (const auto& [logical, clip] : clips) {
						library[logical] = clip;
					}
					j["clips"] = library;
				}
				const std::string anim = GetCurrentAnimationName();
				if (!anim.empty()) {
					j["animation"] = anim;
					j["animation_loop"] = current_animation.loop;
					j["animation_speed"] = current_animation.speed;
				}
				else if (data != nullptr && !data->skeletons.empty() && current_animation.id < 0) {
					//StopAnimation was called on a mesh that *can* animate: an explicit
					//"none", which is authoring data - it is how an entity turns off the
					//animation its template or its mesh's default would otherwise play.
					//A mesh that simply never chose one (SetData leaves id 0) writes no
					//key at all and keeps that default.
					j["animation"] = "";
				}
				return j;
			}

			void Mesh::FromJson(const json& j, const ECS::SerializeContext& ctx) {
				if (ctx.world == nullptr) {
					return;
				}
				//Resolved first and installed once at the end, because attaching an
				//animation set has to happen *before* SetData: SetData caches the first
				//skeleton and sizes the joint buffers from it, so a set attached
				//afterwards would leave the entity holding an animation it cannot play
				//until something else re-set its mesh data.
				Core::MeshData* target = GetData();
				ECS::Entity te = ResolveTemplateEntity(j, ctx);
				if (te != ECS::INVALID_ENTITY_ID) {
					ECS::Coordinator* tc = ctx.world->GetTemplatesCoordinator();
					if (tc->ContainsComponent<Mesh>(te)) {
						target = tc->GetComponent<Mesh>(te).GetData();
					}
				}
				if (j.contains("name") && j["name"].is_string()) {
					const std::string mesh_name = j["name"];
					Core::MeshData* found = ctx.world->GetMeshes().Get(mesh_name);
					if (found == nullptr && mesh_name == World::DEFAULT_MESH_NAME) {
						//As with the default material: built on demand, so a scene
						//referencing it is just the first request for it.
						found = ctx.world->GetDefaultMesh();
					}
					if (found != nullptr) {
						target = found;
					}
					else {
						printf("Mesh::FromJson: unknown mesh '%s'.\n", mesh_name.c_str());
					}
				}
				//A Mesh added from scratch gets the default unit cube: visible in the
				//viewport straight away, and swappable for a real mesh afterwards. A
				//null MeshData would just be an invisible entity that crashes anything
				//reaching for its geometry.
				if (target == nullptr) {
					target = ctx.world->GetDefaultMesh();
				}

				//Animation sets are attached to the (shared) MeshData, which is how the
				//level's own "meshes" section has always done it - so this is the same
				//global act, just expressible per entity and therefore authorable. It is
				//what makes an animation nameable at all: Mesh::SetAnimation only ever
				//searches the sets attached to the mesh.
				bool attached_any = false;
				auto attach_set = [&](const std::string& skeleton_name) {
					std::shared_ptr<Core::Skeleton>* skl =
						ctx.world->GetSkeletons().Get(skeleton_name);
					if (skl == nullptr) {
						printf("Mesh::FromJson: unknown animation set '%s'.\n",
							skeleton_name.c_str());
						return;
					}
					for (const auto& existing : target->skeletons) {
						if (existing == *skl) {
							return;
						}
					}
					target->AddSkeleton(*skl);
					attached_any = true;
				};
				if (target != nullptr && j.contains("skeletons") && j["skeletons"].is_array()) {
					for (const auto& entry : j["skeletons"]) {
						if (entry.is_string()) {
							attach_set(entry.get<std::string>());
						}
					}
				}
				//The animation library, and the sets it implies. A library entry names a
				//clip, and a clip is only playable once the set holding it is attached -
				//so the set is looked up from the clip rather than asked for a second
				//time. That is the whole point of the library: an object lists the
				//animations it has, not the files they arrived in.
				if (target != nullptr && j.contains("clips") && j["clips"].is_object()) {
					clips.clear();
					for (auto it = j["clips"].begin(); it != j["clips"].end(); ++it) {
						if (!it.value().is_string()) {
							continue;
						}
						const std::string clip = it.value();
						if (it.key().empty() || clip.empty()) {
							continue;
						}
						clips[it.key()] = clip;
						const std::string set = ctx.world->FindAnimationSet(clip);
						if (set.empty()) {
							printf("Mesh::FromJson: no loaded animation set holds clip '%s'.\n",
								clip.c_str());
							continue;
						}
						attach_set(set);
					}
				}
				//Re-seating identical data would reset the running animation for nothing;
				//a new attachment is exactly the case where it must be done.
				if (target != GetData() || attached_any) {
					SetData(target);
				}

				//Normal smoothing, and like the animation sets above it is a property of
				//the shared mesh asset that this component is merely the place to author.
				//It replaces the ".NoSmooth" suffix a mesh's node name had to carry: that
				//could only be decided in the modelling tool, and the whole point of the
				//scene editor is that it no longer has to be.
				if (j.contains("smooth") && j["smooth"].is_boolean()) {
					ctx.world->SetMeshSmooth(target, j["smooth"].get<bool>());
				}

				if (j.contains("animation") && j["animation"].is_string()) {
					const std::string anim = j["animation"];
					if (!anim.empty()) {
						SetAnimation(anim, j.value("animation_loop", true), false, -1.0f,
							j.value("animation_speed", 1.0f));
					}
					else {
						//An explicit empty name is "play nothing" (see ToJson), which is a
						//real instruction: an instance of an animated template has no other
						//way to say it should stand still.
						StopAnimation();
					}
				}
			}
		}
	}
}