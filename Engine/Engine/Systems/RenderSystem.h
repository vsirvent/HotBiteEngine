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

#include <tuple>

#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>
#include <Components\Base.h>
#include <Components\Lights.h>
#include <Components\Camera.h>
#include <Components\Particles.h>
#include <Components\Sky.h>
#include <Core\SimpleShader.h>
#include <Core\Material.h>
#include <Core\Mesh.h>
#include <Core\PostProcess.h>
#include <Core\BVH.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {
			class RenderSystem : public ECS::System {
			public:
				static inline ECS::EventId EVENT_ID_PREPARE_ENTITY = ECS::GetEventId<RenderSystem>(0x00);
				static inline ECS::EventId EVENT_ID_UNPREPARE_ENTITY = ECS::GetEventId<RenderSystem>(0x01);
				static inline ECS::EventId EVENT_ID_PREPARE_MATERIAL = ECS::GetEventId<RenderSystem>(0x02);
				static inline ECS::EventId EVENT_ID_UNPREPARE_MATERIAL = ECS::GetEventId<RenderSystem>(0x03);
				static inline ECS::EventId EVENT_ID_DRAW_ENTITY = ECS::GetEventId<RenderSystem>(0x04);
				static inline ECS::EventId EVENT_ID_PREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x05);
				static inline ECS::EventId EVENT_ID_UNPREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x06);

				static inline ECS::EventId EVENT_ID_DEPTH_PREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x07);
				static inline ECS::EventId EVENT_ID_DEPTH_UNPREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x08);

				static inline ECS::EventId EVENT_ID_SHADOW_DIR_LIGHT_PREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x09);
				static inline ECS::EventId EVENT_ID_SHADOW_DIR_LIGHT_UNPREPARE_SHADER = ECS::GetEventId<RenderSystem>(0xa);

				static inline ECS::EventId EVENT_ID_SHADOW_POINT_LIGHT_PREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x0f);
				static inline ECS::EventId EVENT_ID_SHADOW_POINT_LIGHT_UNPREPARE_SHADER = ECS::GetEventId<RenderSystem>(0x10);

				static inline ECS::EventId EVENT_ID_PREPARE_POST = ECS::GetEventId<RenderSystem>(0x11);
				static inline ECS::EventId EVENT_ID_UNPREPARE_POST = ECS::GetEventId<RenderSystem>(0x12);

				static constexpr uint32_t STATIC_SHADOW_REFRESH_PERIOD = 1000;
				static inline ECS::ParamId EVENT_PARAM_SHADER = 0x00;
				static std::recursive_mutex mutex;

				static const std::string WORLD;
				static const std::string PREV_WORLD;
				static const std::string MESH_NORMAL_MAP;
				static const std::string MESH_NORMAL_MAP_ENABLE;
				static const std::string AMBIENT_LIGHT;
				static const std::string DIRLIGHT_COUNT;
				static const std::string DIR_LIGHTS;
				static const std::string POINT_LIGHT_COUNT;
				static const std::string POINT_LIGHTS;
				static const std::string LIGHT_PERSPECTIVE_VALUES;
				static const std::string POINT_SHADOW_MAP_TEXTURE;
				static const std::string DIR_PERSPECTIVE_VALUES;
				static const std::string DIR_STATIC_PERSPECTIVE_VALUES;
				static const std::string DIR_SHADOW_MAP_TEXTURE;
				static const std::string DIR_STATIC_SHADOW_MAP_TEXTURE;
				static const std::string AO_TEXTURE;
				static const std::string ARM_TEXTURE;
				static const std::string EMISSION_TEXTURE;
				static const std::string OPACITY_TEXTURE;
				static const std::string HIGH_TEXTURE;
				static const std::string HIGH_TEXTURE_ENABLED;
				static const std::string NORMAL_TEXTURE;
				static const std::string SPEC_TEXTURE;
				static const std::string MATERIAL;
				static const std::string DIFFUSE_TEXTURE;
				static const std::string DEPTH_TEXTURE;
				static const std::string CAMERA_POSITION;
				static const std::string CAMERA_DIRECTION;
				static const std::string TESS_ENABLED;
				static const std::string VIEW;
				static const std::string PROJECTION;
				static const std::string ACTIVE_VIEWS;
				static const std::string CUBE_VIEW_0;
				static const std::string CUBE_VIEW_1;
				static const std::string CUBE_VIEW_2;
				static const std::string CUBE_VIEW_3;
				static const std::string CUBE_VIEW_4;
				static const std::string CUBE_VIEW_5;
				static const std::string BASIC_SAMPLER;
				static const std::string PCF_SAMPLER;
				static const std::string SCREEN_W;
				static const std::string SCREEN_H;
				static const std::string TIME;
				static const std::string LIGHT_TEXTURE;
				static const std::string TESS_FACTOR;
				static const std::string TESS_TYPE;
				static const std::string DISPLACEMENT_SCALE;

				enum class eRtQuality {
					OFF,
					LOW,
					HIGH
				};

			private:

				struct DrawableEntity {
					Components::Transform* transform = nullptr;
					Components::Mesh* mesh = nullptr;
					Components::Material* mat = nullptr;
					Components::Lighted* lighted = nullptr;
					Components::Base* base = nullptr;
					Components::Bounds* bounds = nullptr;
					DrawableEntity(ECS::Coordinator* c, ECS::Entity entity) {
						transform = &(c->GetComponent<Components::Transform>(entity));
						mesh = &(c->GetComponent<Components::Mesh>(entity));
						base = &(c->GetComponent<Components::Base>(entity));
						mat = &(c->GetComponent<Components::Material>(entity));
						lighted = &(c->GetComponent<Components::Lighted>(entity));
						bounds = &(c->GetComponent<Components::Bounds>(entity));
					}
				};
				ECS::Signature drawable_signature;

				struct ParticleEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::Particles* particles = nullptr;
					ParticleEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						particles = &(c->GetComponent<Components::Particles>(entity));
					}
				};
				ECS::Signature particles_signature;

				struct SkyEntity : public DrawableEntity {
					Components::Sky* sky;
					SkyEntity(ECS::Coordinator* c, ECS::Entity entity) : DrawableEntity(c, entity) {
						sky = &(c->GetComponent<Components::Sky>(entity));
					}
				};
				ECS::Signature sky_signature;

				struct AmbientLightEntity {
					Components::AmbientLight* light;
					AmbientLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						light = &(c->GetComponent<Components::AmbientLight>(entity));
					}
				};
				ECS::Signature amblight_signature;

				struct DirectionalLightEntity {
					Components::DirectionalLight* light;
					DirectionalLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						light = &(c->GetComponent<Components::DirectionalLight>(entity));
					}
				};
				ECS::Signature dirlight_signature;

				struct PointLightEntity {
					Components::Transform* transform;
					Components::PointLight* light;
					PointLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						transform = &(c->GetComponent<Components::Transform>(entity));
						light = &(c->GetComponent<Components::PointLight>(entity));
					}
				};
				ECS::Signature plight_signature;

				struct CameraEntity {
					Components::Base* base;
					Components::Transform* transform;
					Components::Camera* camera;

					CameraEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						camera = &(c->GetComponent<Components::Camera>(entity));
					}
				};
				ECS::Signature camera_signature;

				using RenderTree = std::unordered_map <Core::ShaderKey, std::unordered_map<Core::MaterialData*, std::pair<Components::Material*, ECS::EntityVector<DrawableEntity> > > >;
				using RenderParticleTree = std::map <Core::ShaderKey, std::map<Core::MaterialData*, std::pair<Core::MaterialData*, ECS::EntityVector<ParticleEntity> > > >;

				RenderTree render_tree;
				RenderTree render_pass2_tree;
				RenderTree shadow_tree;
				RenderTree depth_tree;
				RenderParticleTree particle_tree;

				Components::Lighted scene_lighting;

				ECS::EntityVector<AmbientLightEntity> ambient_lights;
				ECS::EntityVector<PointLightEntity> point_lights;
				ECS::EntityVector<DirectionalLightEntity> directional_lights;
				ECS::EntityVector<CameraEntity> cameras;
				ECS::EntityVector<SkyEntity> skies;
				Core::VertexBuffer<Vertex>* vertex_buffer = nullptr;
				ECS::Coordinator* coordinator = nullptr;

				//Temporal buffers for storing multi-texture maps
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_diff = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_norm = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_spec = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_ao = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_arm = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_disp = {};
				std::array<ID3D11ShaderResourceView*, MAX_MULTI_TEXTURE> multitext_mask = {};

				

			public:
				void OnRegister(ECS::Coordinator* c) override;
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				void OnEntityDestroyed(ECS::Entity entity) override;

				void PrepareLights(Core::ISimpleShader* s);
				void UnprepareLights(Core::ISimpleShader* s);

				void PrepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareLights(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

			private:

				Core::DXCore* dxcore = nullptr;
				Core::RenderTexture2D depth_map;
				
				Core::RenderTexture2D light_map;
				Core::RenderTexture2D vol_light_map;
				Core::RenderTexture2D vol_light_map2;
				Core::RenderTexture2D position_map;
				Core::RenderTexture2D prev_position_map;
				Core::RenderTexture2D bloom_map;
				Core::RenderTexture2D temp_map;
				Core::RenderTexture2D rgba_noise_texture;
				Core::RenderTexture2D first_pass_texture;				
				Core::DepthTexture2D depth_view;

				Core::RenderTexture2D texture_tmp;

				Core::RenderTexture2D motion_blur_map;
				
				Core::PostProcess* post_process_pipeline = nullptr;				
				Core::IRenderTarget* first_pass_target = nullptr;
				Core::IRenderTarget* second_pass_target = nullptr;
				Core::IDepthResource* depth_target = nullptr;

				//Motion blur
				Core::SimpleComputeShader* motion_blur = nullptr;

				//Ray tracing
				eRtQuality rt_quality = eRtQuality::HIGH;
				uint32_t RT_TEXTURE_RESOLUTION_DIVIDER = 1;
				static constexpr uint32_t RT_REFLEX_ENABLE = 1;
				static constexpr uint32_t RT_REFRACT_ENABLE = 2;
				static constexpr uint32_t RT_INDIRECT_ENABLE = 4;

				uint32_t rt_enabled = RT_INDIRECT_ENABLE | RT_REFLEX_ENABLE | RT_REFRACT_ENABLE;
				Core::TBVH tbvh{ MAX_OBJECTS };
				Core::SimpleComputeShader* rt_shader = nullptr;
				Core::SimpleComputeShader* rt_denoiser = nullptr;

				//RT texture 1: Reflexed rays
				//RT texture 2: Refracted rays
				//RT texture 3: Indirect rays
				static constexpr int RT_TEXTURE_REFLEX = 0;
				static constexpr int RT_TEXTURE_REFRACT = 1;
				static constexpr int RT_TEXTURE_INDIRECT = 2;
				static constexpr int RT_TEXTURE_REFLEX2 = 3;
				static constexpr int RT_TEXTURE_EMISSION = 4;

				static constexpr int RT_NTEXTURES = 5;
				Core::RenderTexture2D rt_textures[2][RT_NTEXTURES];
				Core::RenderTexture2D* rt_texture_prev;
				Core::RenderTexture2D* rt_texture_curr;
				Core::RenderTexture2D rt_texture_props;
				Core::RenderTexture2D rt_ray_sources0;
				Core::RenderTexture2D rt_ray_sources1;
				Core::RenderTexture2D rt_dispersion;
				Core::ExtBVHBuffer tbvh_buffer;
				Core::BVHBuffer* bvh_buffer = nullptr;
				ObjectInfo objects[MAX_OBJECTS]{};
				MaterialProps objectMaterials[MAX_OBJECTS]{};
				int nobjects = 0;
				ID3D11ShaderResourceView* diffuseTextures[MAX_OBJECTS]{};
				std::mutex rt_mutex;
				std::condition_variable rt_signal;
				bool rt_end = false;
				bool rt_prepare = false;
				std::thread rt_thread;

				//Dust shader
				bool dust_enabled = false;
				bool is_dust_init = false;
				float3 dust_area{};
				float3 dust_offset{};
				Core::RenderTexture2D dust_map;
				Core::RenderTexture2D dust_render_map;
				Core::SimpleComputeShader* dust_init = nullptr;
				Core::SimpleComputeShader* dust_update = nullptr;
				Core::SimpleComputeShader* dust_render = nullptr;

				//Lens flare
				bool lens_flare_enabled = true;
				Core::RenderTexture2D lens_flare_map;
				Core::SimpleComputeShader* lens_flare = nullptr;
				Core::BaseDOFProcess* dof_effect = nullptr;

				//Volumetric lights
				Core::SimpleComputeShader* vol_shader = nullptr;
				Core::SimpleComputeShader* blur_shader = nullptr;
				struct VoumetricLightData {
					uint32_t golbal_illumination;
					float3 padding;
				};
				Core::RenderTexture2D vol_data;

				//Texture mixer
				Core::SimpleComputeShader* mixer_shader = nullptr;

				//Anti Alias
				Core::SimpleComputeShader* aa_shader = nullptr;

				//Motion texture
				Core::RenderTexture2D motion_texture;
				Core::SimpleComputeShader* motion_shader = nullptr;

				float time = 0.0f;
				bool tess_enabled = true;
				bool normal_material_map = true;
				bool normal_mesh_map = true;
				bool wireframe_enabled = false;
				bool aa_enabled = true;
				bool motion_blur_enabled = true;
				bool dof_enabled = true;
				uint32_t rt_debug = 0;
				uint32_t frame_count = 0;
				uint32_t current = 0;
				uint32_t prev = 1;

				bool cloud_test = false;

				void DrawSky(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection);
				void CastShadows(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, bool static_shadows);
				void DrawDepth(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection);
				void DrawScene(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection,
					ID3D11ShaderResourceView* prev_pass_texture,
					Core::IRenderTarget* target, RenderTree& tree);

				void LoadRTResources();
				void ProcessMotion();
				void PrepareRT();
				void ProcessRT();
				void ProcessDust();
				void ProcessLensFlare();
				void ProcessMotionBlur();
				void ProcessMix();
				void ProcessAntiAlias();

				void DrawParticles(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, RenderParticleTree& tree);
				bool IsVisible(const float3& camera_pos, const DrawableEntity& drawable, const matrix& view_projection, int w, int h) const;
				void CheckSceneVisibility(RenderTree& tree);
				void PostProcessLight();
				
				void PrepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

				void PrepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareMultiMaterial(Components::Material* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

				void PrepareEntity(DrawableEntity& entity, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareEntity(DrawableEntity& entity, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void SetEntityLights(Components::Lighted* lighted, ECS::EntityVector<DirectionalLightEntity>& dir_lights, ECS::EntityVector<PointLightEntity>& point_lights);
				
				void AddDrawable(ECS::Entity entity, const Core::ShaderKey& key, Components::Material* mat, RenderTree& tree, const RenderSystem::DrawableEntity& drawable);
				void AddParticle(ECS::Entity entity, const Core::ShaderKey& key, Core::MaterialData* mat, RenderParticleTree& tree, const RenderSystem::ParticleEntity& particle);
				void RemoveParticle(ECS::Entity entity, RenderParticleTree& tree);
				void RemoveDrawable(ECS::Entity entity, RenderTree& tree);

				void PrepareVolumetricShader(Core::ISimpleShader* ps);
				void UnprepareVolumetricShader(Core::ISimpleShader* ps);
				
			public:
				
				RenderSystem() = default;
				~RenderSystem();

				//System methods
				bool Init(Core::DXCore* dx_core, Core::VertexBuffer<Vertex>* vb, Core::BVHBuffer* bvh = nullptr);
				void Clear(const float color[4]);
				void SetPostProcessPipeline(Core::PostProcess* pipeline);
				void Draw();
				void Update();
				
				//Render parameters
				void EnableTessellation(bool enabled);
				bool IsEnabledTessellation() const;
				void EnableNormalMaterialMapping(bool enabled);
				bool IsEnabledEnableNormalMaterialMapping() const;
				void EnableNormalMeshMapping(bool enabled);
				bool IsEnabledEnableNormalMeshMapping() const;
				void SetWireframe(bool enabled);
				bool GetWireframe() const;
				void SetCloudTest(bool enabled);
				bool GetCloudTest() const;
				void SetRayTracingQuality(eRtQuality quality);
				eRtQuality GetRayTracingQuality() const;
				void SetRayTracing(bool reflex_enabled, bool refract_enabled, bool indirect_enabled);
				void GetRayTracing(bool& reflex_enabled, bool& refract_enabled, bool& indirect_enabled) const;
				void SetDustEnabled(bool enabled);
				bool GetDustEnabled() const;
				void SetDustEffectArea(int32_t num_particles, const float3& area, const float3& offset);
				void SetLensFlare(bool enabled);
				bool GetLensFlare() const;
				void SetAA(bool enabled);
				bool GetAA() const;
				void SetMotionBlur(bool enabled);
				bool GetMotionBlur() const;
				void SetDOF(bool enabled);
				bool GetDOF() const;
				void SetRTDebug(uint32_t debug);
				uint32_t GetRTDebug() const;
			};
		}
	}
}
