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

#include <Defines.h>
#include <ECS/Types.h>
#include <ECS/Serialization.h>
#include <Core/Material.h>
#include <Core/Mesh.h>
#include <Core/SplatCloud.h>
#include <Core/Utils.h>
#include <Core/SpinLock.h>
#include <Core/Scheduler.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <map>
#include <string>

namespace HotBite {
	namespace Engine {
		namespace Components {

			/**
			 * The draw method of the entity
			 */
			enum eDrawMethod {
				//Render always
				DRAW_ALWAYS,
				//Render only if inside screen
				DRAW_SCREEN
			};

			/**
			 * The base component, every entity has this component
			 */
			struct Base {
				static constexpr const char* NAME = "Base";

				std::string name;
				//Entity Id
				ECS::Entity id = ECS::INVALID_ENTITY_ID;
				//Parent entity
				ECS::Entity parent = ECS::INVALID_ENTITY_ID;
				//Use parent position
				bool parent_rotation = true;
				//Use parent rotation
				bool parent_position = true;
				//The joint of the parent's skeleton this entity rides, "" for none.
				//
				//With a bone, the entity's Transform is an offset *from that joint in the
				//parent mesh's own space*, and its world matrix becomes
				//local * joint * parent_world - the parent's whole world matrix, so the
				//parent's scale reaches the child too. That is what makes a weapon follow
				//a hand through an animation instead of hanging at the model's origin, and
				//it is the only parenting path that reads more than the parent's position
				//and rotation (`parent_position`/`parent_rotation` do not apply here).
				//
				//Round-trips by name, like `parent` itself; the index it resolves to is
				//per-session, so it is cached rather than stored.
				std::string parent_bone;
				//Cached index of `parent_bone` in the parent mesh's skeleton. -1 is "no
				//bone", -2 "not looked up yet"; anything that changes `parent_bone` or the
				//parent must reset it to UNRESOLVED_JOINT.
				static constexpr int UNRESOLVED_JOINT = -2;
				int parent_joint = UNRESOLVED_JOINT;
				//Render only when vertex in screen or always
				eDrawMethod draw_method = eDrawMethod::DRAW_SCREEN;
				//The pass where this entity is rendered
				uint32_t pass = 1;
				//Entity visible
				bool visible = true;
				//Scene visible
				bool scene_visible = true;
				//Entity used for depth-z texture
				bool draw_depth = true;
				//Entity casts shadows
				bool cast_shadow = true;
				//Casts a shadow but is never drawn in the main color pass - the role of a
				//splat cloud's inferred low-poly proxy mesh (see World::AttachSplatProxy):
				//the cloud's own appearance is already painted by the splat compute pass,
				//so the proxy would otherwise double-draw its (approximate) silhouette on
				//top of it, and if it also reached the camera's depth pre-pass it could
				//misfire the splat preprocessor's occlusion test against that same
				//approximation. Default false, so every existing entity is unaffected.
				bool shadow_caster_only = false;
				//Entity is static
				bool is_static = false;
				//Creation time
				int64_t creation_time = Core::Scheduler::GetNanoSeconds();

				//`name`, `id` and `creation_time` are identity/runtime state, not authoring
				//data: the entity's name is the key of the record this block lives in, and
				//ids are recycled across sessions. `parent` round-trips by name.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			/**
			 * The transform component: Includes all the position, roation, scale information of the entity.
			 */
			struct Transform {
				static constexpr const char* NAME = "Transform";
				//Event triggered when transform is changed
				static inline ECS::EventId EVENT_ID_TRANSFORM_CHANGED = ECS::GetEventId<Transform>(0x00);
				//The cached last parent position to check if parent has changed
				float3 last_parent_position = {};
				float4 last_parent_rotation = {};
				//The entity position
				float3 position = { 0.0f, 0.0f, 0.0f };
				//The entity scale
				float3 scale = { 1.0f, 1.0f, 1.0f };
				//The entity initial rotation
				float4 initial_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
				//The entity rotation
				float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
				//The entity world matrix ready to be used in the vertex shader
				float4x4 world_matrix = {};
				//The world matrix the previous *rendered* frame used, which is what a motion
				//vector is measured against. Latched once per frame by RenderSystem::Draw and
				//by nothing else: the transform systems run on the background thread at their
				//own rate, so a system that latched it there erased the frame's motion on
				//every tick where the entity had not moved (the same trap
				//Camera::prev_view_projection fell into).
				float4x4 prev_world_matrix = {};
				//The entity inverse world matrix ready to be used in the vertex shader
				float4x4 world_inv_matrix = {};
				//The entity world matrix to be used in the application
				matrix world_xmmatrix = {};
				//True if the world matrix is dirty and needs to be recalculated
				bool dirty = true;

				//Only position/rotation/scale are authoring data. The world matrices, the
				//cached parent pose and `dirty` are all recomputed by the transform system
				//from those three, so writing them would just be a stale duplicate.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);

				static void Rotate(struct Transform& t, const float3& axis, float value) {
					static float rot_val = 0.0f;
					auto rot = DirectX::XMMatrixRotationAxis({ axis.x, axis.y, axis.z }, rot_val);
					vector3d move_q = DirectX::XMQuaternionRotationMatrix(rot);
					rot_val += 0.005f;
					if (rot_val > DirectX::XM_PI) rot_val = -DirectX::XM_PI;
					vector3d rot_q = DirectX::XMVectorSet(t.initial_rotation.x, t.initial_rotation.y, t.initial_rotation.z, t.initial_rotation.w);
					auto r = DirectX::XMQuaternionMultiply(rot_q, move_q);
					DirectX::XMStoreFloat4(&t.rotation, r);
					t.dirty = true;
				}
			};

			/**
			 * The component that contains the entity bounds.
			 */
			struct Bounds {
				static constexpr const char* NAME = "Bounds";

				//The entity bounding box in local space
				box local_box = {};
				//The entity x,y,z aligned bounding box in world space and after transformations
				box final_box = {};
				//The entity oriented bounding box in world space and after transformations
				orientedBox bounding_box = {};

				//Only the local box is authored; the world-space boxes are derived from it
				//and the Transform every frame.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			//The layer stack a material can draw with. It lives on Core::MaterialData
			//(and in the world's registry) rather than here, because the render trees are
			//keyed by material: every entity in a bucket is drawn with one set of layer
			//constants, so a per-entity stack could only ever be honoured for whichever
			//entity the bucket happened to hold first. Aliased here under its old name so
			//existing tool code keeps compiling.
			using MultiMaterial = Core::MultiMaterialData;

			/**
			 * The component that contains the entity materials
			 */
			struct Material {
				static constexpr const char* NAME = "Material";

				//We can reuse a material in several components
				Core::MaterialData* data;

				//Serializes as the material's *name* ("floor"), resolved against the world's
				//material collection on load - a MaterialData pointer is shared between
				//every entity using it and means nothing across sessions. Also accepts
				//"template": <template entity name>, which adopts that template's material,
				//the behaviour the old top-level "template" key had.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			/**
			 * Component representing a 3D mesh that can be animated.
			 * This class extends the ECS::IEventSender class to allow it to send events when animations end.
			 * It also contains various properties and methods related to animating the mesh, such as
			 * current and previous animations, animation speed, and methods for setting and updating animations.
			 */
			class Mesh : public ECS::IEventSender {
			public:
				static constexpr const char* NAME = "Mesh";
				//Event triggered when animation ends
				static inline ECS::EventId EVENT_ID_ANIMATION_END = ECS::GetEventId<Mesh>(0x01);
				static inline ECS::EventId EVENT_ID_ANIMATION_FRAME_EVENT = ECS::GetEventId<Mesh>(0x02);
				//Animation parameter id
				static inline ECS::ParamId EVENT_PARAM_ANIMATION_ID = 0x00;
				//Animation name parameter id
				static inline ECS::ParamId EVENT_PARAM_ANIMATION_NAME = 0x01;
				//Animation event frame
				static inline ECS::ParamId EVENT_PARAM_ANIMATION_FRAME = 0x02;
				//Animation event id
				static inline ECS::ParamId EVENT_PARAM_ANIMATION_FRAME_ID = 0x04;

				std::vector<matrix> joint_cpu_data;
				std::vector<Core::JointGpuData> joint_gpu_data;
				//The skinning matrices as of the previous *rendered* frame, uploaded next to
				//the current ones so the vertex shader can skin each vertex twice and report
				//where it was. Without them an animation that plays in place produces no
				//motion at all: the entity's world matrix is the same all frame, and it is the
				//only other thing the previous position could come from.
				//
				//Latched by RenderSystem::Draw, like Transform::prev_world_matrix and for the
				//same reason - Mesh::Update runs on the background thread on a timer of its
				//own, so latching there measures one animation tick rather than one frame.
				std::vector<Core::JointGpuData> prev_joint_gpu_data;
				//Where each joint *is* for the pose currently playing, in the mesh's own
				//space - the animation matrix before the inverse bind pose is folded in,
				//which is what an attachment needs and what a skinning matrix is not
				//(that one maps bind-pose vertices, not a socket).
				//
				//Only filled while TrackJoints() is on, because it costs a matrix store per
				//joint per frame and nothing but an attachment ever reads it.
				//
				//It has a lock of its own rather than riding `skeleton_mutex`: the reader is
				//StaticMeshSystem, which runs holding physics_mutex, while Mesh::Update
				//sends animation events from *inside* skeleton_mutex - so taking that one
				//here would invert the two (the deadlock GetLocalBox's comment describes).
				//Nothing is done under this lock but a swap and a copy, so it cannot invert
				//against anything. It cannot be dropped either, the way GetLocalBox drops
				//skeleton_mutex: that reads a box out of the immortal MeshData, while this
				//vector is resized when the skeleton changes, and reading one mid-resize is
				//a use-after-free rather than a stale value.
				std::vector<matrix> joint_pose_data;
				//Where Update builds the above before publishing it, so the poses are
				//computed (and animation events sent) outside the lock.
				std::vector<matrix> joint_pose_scratch;
				mutable Core::spin_lock joint_pose_lock;

				struct Animation {
					std::string name;
					int id = -1;
					uint64_t t0 = 0;
					uint64_t t1 = 0;
					int key_frame = -1;
					float speed = 1.0f;
					std::shared_ptr<Core::Skeleton> skeleton = nullptr;
					bool loop = true;
					bool sync = false;
				};

				Animation current_animation;
				Animation previous_animation;

				//The mesh's animation library: the logical name this entity knows an
				//animation by -> the clip that actually plays, e.g. "idle" -> "troll_idle".
				//
				//This is what makes an animation belong to the *object* rather than to the
				//file it was imported from. A template names the roles its instances play
				//("idle", "walk", "attack"); which clip fills a role is an asset decision
				//that can change - re-export the walk cycle from another FBX and only the
				//library entry moves. Game code then reads as
				//SetAnimation("walk") instead of SetAnimation("troll_walk"), and the same
				//code drives a troll, a zombie or an archer.
				//
				//Empty is the old behaviour exactly: every SetAnimation name is a clip
				//name, resolved against the sets attached to the mesh.
				std::map<std::string, std::string> clips;

				float animation_change_current_time = 1000.0f;
				float animation_default_change_time = 250.0f;
				float animation_change_time = animation_default_change_time;
				//What DrawIndexed is called with, and the only thing a level of detail
				//changes about an entity: SetLod points them at another mesh in the
				//asset's chain, `data` and everything measured from it stay put.
				uint32_t index_count = 0;
				size_t index_offset = 0;
				size_t vertex_offset = 0;
				//The level being drawn: an index into GetData()->lods, 0 for the full
				//mesh and for every mesh that declares no chain. Owned by
				//RenderSystem::SelectLods, which sets it once per frame before anything
				//draws - the depth pre-pass and the main pass have to agree on the
				//geometry or the second one is rejected by the depth the first recorded.
				int current_lod = 0;
				//Whether this entity follows its mesh's chain at all. Per entity and not
				//per asset, because it is the one part of this that is about a particular
				//object rather than about the geometry: the hero the camera is always
				//looking at is pinned to full detail while every other instance of the
				//same mesh drops away normally.
				bool lod_enabled = true;
				uint32_t time_offset = rand();
				
				ECS::Entity entity = ECS::INVALID_ENTITY_ID;
				ECS::Event end_animation_event;
				ECS::Coordinator* coordinator = nullptr;
				std::shared_ptr<std::recursive_mutex> skeleton_mutex;

			private:
				//We can reuse a mesh in several components
				Core::MeshData* data = nullptr;
				//Set while something is attached to a joint of this mesh; see
				//joint_pose_data.
				bool track_joints = false;
				matrix GetAnimationMatrix(int64_t elapsed_nsec, int64_t total_nsec,
					std::vector<Core::JointCpuData>* cpu_data,
					int joint_id, Animation& anim, matrix* pose = nullptr);

			public:
				Mesh();
				virtual ~Mesh();
				void SetData(Core::MeshData* data);
				Core::MeshData* GetData();
				// Draws level `index` of the mesh's chain from now on: the three
				// DrawIndexed arguments, and nothing else. Out of range (and any index
				// but 0 on a mesh with no chain) falls back to the full mesh rather than
				// leaving the entity drawing whatever it drew before.
				void SetLod(int index);
				void SetCoordinatorInfo(ECS::Entity e, ECS::Coordinator* c);
				// Plays `name`, which is either a logical name from `clips` or a clip name
				// directly - the library is consulted first, so an object that publishes
				// "walk" keeps working when the clip behind it is re-imported under a new
				// name. What the mesh reports afterwards (GetCurrentAnimationName, and
				// therefore what gets serialized) is the name it was *asked* for, so the
				// vocabulary an entity was authored with round-trips.
				bool SetAnimation(const std::string& name, bool loop = true, bool sync = false,
					              float transition_time = -1.0f, float speed = 1.0f, bool force = false);
				// The clip `name` resolves to: the library entry when there is one, `name`
				// itself otherwise. Anything looking an animation up in the mesh data
				// (a preview, an editor listing) has to go through this or it will search
				// for a logical name no skeleton has ever heard of.
				std::string ResolveClip(const std::string& name) const;
				// Stops whatever is playing: the mesh falls back to its bind pose and
				// stays there until something sets an animation again. The attached
				// skeletons are left alone, so this is reversible with SetAnimation.
				//
				// Distinct from "never chose an animation", which SetData leaves behind
				// (the first animation of the first skeleton, playing): this is the
				// deliberate "none", and it is what a per-entity override needs to turn
				// off an animation its template started.
				void StopAnimation();
				void SetAnimationDefaultTransitionTime(float time);
				float GetAnimationDefaultTransitionTime() const;
				int GetCurrentAnimationId() const;
				std::string GetCurrentAnimationName() const;
				// The box this mesh occupies in its own local space: the extents of the
				// animation it is playing when the mesh is skinned (Core::MeshData::
				// GetAnimationBox), the stored vertex extents otherwise. False when there
				// is no mesh data to measure.
				//
				// Anything measuring a Bounds must come through here. Reading
				// minDimensions/maxDimensions directly gives the bind pose, which for a
				// rig is a T-pose: the demo troll's arms are spread in the buffer and
				// nowhere in any of its animations, so its box came out nearly twice as
				// wide as the model, and its collider with it.
				//
				// A transition between two animations is measured as the one being blended
				// *to*; for the 250ms it lasts the box can be a little tight at the
				// silhouette of the one being left behind.
				bool GetLocalBox(box& out) const;
				int GetCurrentFrame() const;

				// == Joint sockets ====================================================
				// What an attachment rides (Base::parent_bone). The index is into the
				// skeleton currently playing, so it is only valid while that clip is the
				// one selected - which is why callers cache it and re-resolve rather than
				// storing a matrix.

				// The index of the joint named `name`, or -1 when this mesh has no
				// skeleton or no joint by that name. Case sensitive, matching the names
				// the FBX carried.
				int FindJoint(const std::string& name) const;
				// The names of the joints of the skeleton this mesh animates with, in
				// index order - the list a bone picker offers. Empty for an unskinned mesh.
				std::vector<std::string> GetJointNames() const;
				// Asks Update to keep joint_pose_data filled. Anything reading
				// GetJointPose must have turned this on first; it stays on for the life of
				// the component, since attachments come and go far more often than the
				// cost of a matrix store per joint matters.
				void TrackJoints();
				bool IsTrackingJoints() const { return track_joints; }
				// Where joint `index` sits for the pose being played, in this mesh's own
				// space. False when the index is out of range, when nothing is playing, or
				// when TrackJoints was never called - in every one of which the caller must
				// fall back to the un-socketed composition rather than to identity, which
				// would slam the attachment onto the model's origin.
				bool GetJointPose(int index, matrix& out) const;

				void Update(int64_t elapsed_nsec, int64_t total_nsec);
				void Prepare(Core::SimpleVertexShader* vs);
				void Unprepare(Core::SimpleVertexShader* vs);
				// Keeps the pose this frame was drawn with as the one the next frame measures
				// its motion against (prev_joint_gpu_data). Called once per rendered frame,
				// from RenderSystem::Draw, *after* the frame is drawn - see the member.
				void LatchPrevJoints();
				const std::vector<matrix>& GetJoints() { return joint_cpu_data; }

				//Serializes as the mesh asset's name, the animation sets attached to it
				//and the current animation, all resolved against the world's collections
				//on load. Joint buffers, offsets and timing are runtime state rebuilt
				//from the asset. Also accepts "template": <template entity name> to adopt
				//a template's mesh, matching the old top-level "template" key.
				//
				//"skeletons" is the per-entity form of the level's "meshes" section: the
				//named animation sets to attach to this mesh. It is what makes
				//"animation" mean anything - SetAnimation only searches the sets attached
				//to the MeshData - and, exactly as in that section, the attachment is to
				//the *shared* mesh asset and so is visible to every entity using it.
				//
				//"clips" is the animation library ({"idle": "troll_idle", ...}), and it
				//carries its own attachment: whichever set owns a clip is attached when the
				//library names it, so an object that lists its animations never has to list
				//the files they came from as well. "skeletons" stays for the case with no
				//library - and for attaching a set whose clips are only chosen at runtime.
				//
				//"smooth" is normal smoothing (World::SetMeshSmooth), and belongs to the
				//shared mesh asset for exactly the same reason "skeletons" does. It
				//supersedes the ".NoSmooth" suffix in an .fbx node name, which is still
				//read at import as the default - it is the only way the existing models
				//say it, and a level that carries no "smooth" key keeps it.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			/**
			 * Draws a cloud of 3D Gaussians instead of a triangle mesh.
			 *
			 * The peer of Mesh, and mutually exclusive with it in practice - an entity
			 * carrying both draws both, which is legal but is almost never what anyone
			 * meant.
			 *
			 * What makes this different from a normal Gaussian-splatting renderer is what
			 * the splats *carry*. A capture stores baked radiance and a splat renderer
			 * composites it; here each splat carries albedo, a normal and a specular
			 * intensity, and RenderSystem's splat pass lights them with the scene's own
			 * lights and shadows before writing the result into the same targets
			 * MainRenderPS writes. So a cloud sits under the level's sun, takes its
			 * shadows, moves through its post-process chain, and is visible to the ray
			 * tracers as a ray source. See Core::SplatCloudData for the cost of that
			 * reinterpretation.
			 *
			 * Asset scope vs entity scope follows Mesh exactly: the splats, their count and
			 * the cloud's bounds belong to the shared Core::SplatCloudData, and everything
			 * here is either a per-entity multiplier over it or renderer bookkeeping.
			 */
			struct SplatCloud {
				static constexpr const char* NAME = "SplatCloud";

				//The shared cloud. Serializes as its *name*, resolved against the world's
				//splat cloud collection on load - the pointer means nothing across sessions.
				Core::SplatCloudData* data = nullptr;

				//Scales every splat's alpha. The fade-out knob, and the only way to make a
				//cloud translucent without re-importing it.
				float opacity_scale = 1.0f;
				//Scales every splat's albedo. The exposure match: an imported capture's
				//colours are radiance being reinterpreted as reflectance, so they routinely
				//land too bright or too dark next to authored materials, and this is the
				//correction that does not require touching the file.
				float albedo_scale = 1.0f;
				//Specular intensity handed to CalcDirectional/CalcPoint, overriding what the
				//import guessed. A capture carries no specular information at all - every
				//highlight it saw is already inside the albedo - so this is authored, never
				//recovered.
				float spec_intensity = 0.1f;

				//Scales every splat's size (its covariance, so quadratically). A trained
				//3DGS .ply has no use for this - scale is per-splat data the reference
				//trainer fit - but a plain coloured point cloud (no Gaussian parameters at
				//all: see SplatCloudData::Load) has none of its own, so the loader gives
				//every point the same guessed radius from the cloud's extent and point
				//count. That guess is baked into the loaded data at import time and cannot
				//itself be undone without reimporting; this is the live knob on top of it,
				//for the case the guess reads too sparse (gaps between points) or too dense
				//(a blurred, over-thick surface).
				float point_size_scale = 1.0f;

				//Flips every normal. The import points normals away from the cloud centroid,
				//which is right for an object scanned from the outside and exactly backwards
				//for a room scanned from within; nothing in the point set distinguishes the
				//two, so it is a switch rather than a heuristic.
				bool invert_normals = false;

				//How opaque the cloud has to be at a pixel before it owns that pixel's
				//G-buffer - depth, world position, normal and the ray sources. Below it the
				//cloud still contributes colour, over whatever is behind it, but leaves the
				//surface record to that. A cloud always writes something: whether the object
				//renders at all is Base::visible's job, and a splat cloud that contributed
				//colour but no depth would be a third state neither flag describes.
				//
				//It used to decide something else as well - where along the front-to-back
				//accumulation the surface was declared to be, and so which splats were
				//averaged into it. SplatRasterCS composites with transmittance now, which
				//settles that per splat rather than by a threshold, so this is purely the
				//ownership test.
				float surface_alpha = 0.5f;

				//The most splats this cloud is worth drawing per screen pixel. The
				//renderer keeps a stable random subset of the cloud sized to hold this
				//many over the area it projects to, and drops the rest before they are
				//even loaded.
				//
				//It is a *density*, not a count, so one number covers both jobs: it thins
				//a cloud as it recedes (the area shrinks, so the subset does), and it caps
				//an over-dense capture at its closest - a model carrying far more splats
				//than its silhouette can show is thinned even at full size, which is the
				//knob for optimising one without re-importing it.
				//
				//Well above 1 by default, and that matters: the rasterizer averages
				//albedo and normal, so a random subset has the same mean, but *coverage*
				//scales with the fraction kept. Too low and a covered pixel's alpha stops
				//saturating and the cloud turns translucent rather than merely coarser.
				//Around 4 is where that starts to show on a scanned surface.
				float max_density = 16.0f;

				//Every field is written unconditionally, including ones that match their
				//default. Undo replays an earlier ToJson (ComponentOps::RecordEdit) and every
				//FromJson reads a missing key as "leave alone", so a key omitted because it
				//happened to match the default at save time cannot be restored - the undo
				//reports success and changes nothing. Same trap Mesh's "smooth" documents.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			struct Player {
				static constexpr const char* NAME = "Player";

				//A tag component: presence is the whole payload, so it serializes as an
				//empty object. FromJson still has to exist for the concept to match.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const {
					return nlohmann::json::object();
				}
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx) {}
			};
		}
	}
}