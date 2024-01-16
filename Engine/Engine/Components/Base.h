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
#include <Core/Material.h>
#include <Core/Mesh.h>
#include <Core/Utils.h>
#include <Core/SpinLock.h>
#include <d3d11.h>

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
				std::string name;
				//Entity Id
				ECS::Entity id = ECS::INVALID_ENTITY_ID;
				//Parent entity
				ECS::Entity parent = ECS::INVALID_ENTITY_ID;
				//Use parent position
				bool parent_rotation = true;
				//Use parent rotation
				bool parent_position = true;
				//Render only when vertex in screen or always
				eDrawMethod draw_method = eDrawMethod::DRAW_ALWAYS;
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
				//Entity is static
				bool is_static = false;
			};

			/**
			 * The transform component: Includes all the position, roation, scale information of the entity.
			 */
			struct Transform {
				//Event triggered when transform is changed
				static inline ECS::EventId EVENT_ID_TRANSFORM_CHANGED = ECS::GetEventId<Transform>(0x00);
				//The cached last parent position to check if parent has changed
				float3 last_parent_position = {};
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
				//The entity world matrix to be used in the application
				matrix world_xmmatrix = {};
				//True if the world matrix is dirty and needs to be recalculated
				bool dirty = true;
			};

			/**
			 * The component that contains the entity bounds.
			 */
			struct Bounds {
				//The entity bounding box in local space
				box local_box = {};
				box bounding_box = {};
				//The entity bounding box in world space and after transformations
				box final_box = {};
			};

			/**
			 * The component that contains the entity materials
			 */
			struct Material {
				//Maximum multiple texture count
				#define MAX_MULTI_TEXTURE 8
				//Texture layer mix operation: mix
				#define TEXT_OP_MIX 1
				//Texture layer mix operation: add
				#define TEXT_OP_ADD 2
				//Texture layer mix operation: multiplication
				#define TEXT_OP_MULT 3

				//Texture type flags
				#define TEXT_DIFF (1 << 3)
				#define TEXT_NORM (1 << 4)
				#define TEXT_SPEC (1 << 5)
				#define TEXT_ARM  (1 << 6)
				#define TEXT_DISP (1 << 7)
				#define TEXT_AO   (1 << 8)
				#define TEXT_MASK   (1 << 9)
				#define TEXT_UV_NOISE   (1 << 12)
				#define TEXT_MASK_NOISE   (1 << 13)

				//We can reuse a material in several components
				Core::MaterialData* data;
				//Multimaterial data painted over main "data" material
				uint32_t multi_texture_count = 0;
				float multi_parallax_scale = 0.0f;
				uint32_t tessellation_type = 0;
				float tessellation_factor = 0;
				float displacement_scale = 0.0f;

				std::vector<Core::MaterialData*> multi_texture_data;
				std::vector<ID3D11ShaderResourceView*> multi_texture_mask;
				std::vector<uint32_t> multi_texture_operation;
				std::vector<float> multi_texture_value;
				std::vector<float> multi_texture_uv_scales;

				bool LoadMultitexture(const std::string& json_str, const std::string& root_path, const Core::FlatMap<std::string, Core::MaterialData>& materials);
			};

			/**
			 * Component representing a 3D mesh that can be animated.
			 * This class extends the ECS::IEventSender class to allow it to send events when animations end.
			 * It also contains various properties and methods related to animating the mesh, such as
			 * current and previous animations, animation speed, and methods for setting and updating animations.
			 */
			class Mesh : public ECS::IEventSender {
			public:
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

				float animation_change_current_time = 1000.0f;
				float animation_default_change_time = 250.0f;
				float animation_change_time = animation_default_change_time;
				uint32_t index_count = 0;
				size_t index_offset = 0;
				size_t vertex_offset = 0;
				uint32_t time_offset = rand();
				
				ECS::Entity entity = ECS::INVALID_ENTITY_ID;
				ECS::Event end_animation_event;
				ECS::Coordinator* coordinator = nullptr;
				std::shared_ptr<std::recursive_mutex> skeleton_mutex;

			private:
				//We can reuse a mesh in several components
				Core::MeshData* data = nullptr;
				matrix GetAnimationMatrix(int64_t elapsed_nsec, int64_t total_nsec,
					std::vector<Core::JointCpuData>* cpu_data,
					int joint_id, Animation& anim);

			public:
				Mesh();
				virtual ~Mesh();
				void SetData(Core::MeshData* data);
				Core::MeshData* GetData();
				void SetCoordinatorInfo(ECS::Entity e, ECS::Coordinator* c);
				bool SetAnimation(const std::string& name, bool loop = true, bool sync = false,
					              float transition_time = -1.0f, float speed = 1.0f, bool force = false);
				void SetAnimationDefaultTransitionTime(float time);
				float GetAnimationDefaultTransitionTime() const;
				int GetCurrentAnimationId() const;
				std::string GetCurrentAnimationName() const;
				int GetCurrentFrame() const;
				void Update(int64_t elapsed_nsec, int64_t total_nsec);
				void Prepare(Core::SimpleVertexShader* vs);
				void Unprepare(Core::SimpleVertexShader* vs);
				const std::vector<matrix>& GetJoints() { return joint_cpu_data; }				
			};

			struct Player {
			};
		}
	}
}