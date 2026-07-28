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
				static const std::string AUTOFOCUS_TEXTURE;
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
					MID,
					HIGH
				};

				//What the frame is made of, as the texture mixer sees it. Selecting one
				//makes ProcessMix write that buffer to the screen instead of the mixed
				//frame - the last point at which every contribution still exists
				//separately.
				//
				//The values, and the RT_DEBUG_* flags below, MUST match
				//Shaders/Common/RenderDebug.hlsli: they cross into HLSL as a bare uint
				//in the `debug` field of three cbuffers and nothing validates them.
				enum class eDebugBuffer : uint32_t {
					OFF = 0,
					SCENE,          //material colour before any light is applied
					LIGHT,          //direct light accumulation
					BLOOM,
					EMISSION,
					REFLECTION,     //ray-traced reflections
					REFRACTION,     //ray-traced refractions
					INDIRECT,       //ReSTIR global illumination
					VOLUMETRIC,
					DUST,
					LENS_FLARE,
					DEPTH,          //world distance, exponentially mapped
					POSITION,       //world position, 10-unit repeating ramp
					NORMAL,         //world normal, remapped to 0..1
					MOTION,         //screen-space motion, velocity-buffer encoding
					COUNT
				};

				//Bypasses, independent of the buffer selection and of each other: each
				//turns its denoiser into a copy so the buffer downstream carries the raw
				//traced signal. Pair one with the matching buffer view to see what the
				//ray tracer actually produced.
				static constexpr uint32_t RT_DEBUG_BUFFER_MASK = 0xFF;
				static constexpr uint32_t RT_DEBUG_NO_GI_DENOISE = 0x100;
				static constexpr uint32_t RT_DEBUG_NO_RT_DENOISE = 0x200;

				//Names in eDebugBuffer order, for menus and for the automation command.
				static const char* DebugBufferName(eDebugBuffer buffer);

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

				//Every drawable, once, regardless of which pass or shader bucket it landed in.
				//The trees are keyed by shader tuple and material, which is what a draw call
				//needs but no use at all for the one thing that has to happen exactly once per
				//entity per frame: latching what this frame was drawn with as the previous
				//frame (LatchPreviousFrame).
				ECS::EntityVector<DrawableEntity> drawables;

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
				
				Core::RenderTexture2D* current_light_map = nullptr;
				Core::RenderTexture2D* prev_light_map = nullptr;
				Core::RenderTexture2D light_map[2];

				Core::RenderTexture2D vol_light_map;
				Core::RenderTexture2D vol_light_map2;
				Core::RenderTexture2D position_map;
				Core::RenderTexture2D prev_position_map;
				Core::RenderTexture2D bloom_map;
				Core::RenderTexture2D temp_map;
				Core::RenderTexture2D rgba_noise_texture;
				Core::RenderTexture2D first_pass_texture;
				//The frame's one depth buffer. DrawDepth fills it, then DrawSky and
				//DrawScene render against it so the hardware can reject what it already
				//proved occluded. Nothing else in the engine owns a scene depth buffer:
				//the back buffer's and any post-process pipeline's go unused.
				Core::DepthTexture2D depth_view;

				Core::RenderTexture2D texture_tmp;

				Core::RenderTexture2D motion_blur_map;
				
				Core::PostProcess* post_process_pipeline = nullptr;				
				Core::IRenderTarget* first_pass_target = nullptr;
				Core::IRenderTarget* second_pass_target = nullptr;

				//Motion blur
				Core::SimpleComputeShader* motion_blur = nullptr;

				//Ray tracing
				eRtQuality rt_quality = eRtQuality::MID;
				uint32_t RT_TEXTURE_RESOLUTION_DIVIDER = 1;
				static constexpr uint32_t RT_REFLEX_ENABLE = 1;
				static constexpr uint32_t RT_REFRACT_ENABLE = 2;
				static constexpr uint32_t RT_INDIRECT_ENABLE = 4;

				uint32_t rt_enabled = RT_INDIRECT_ENABLE | RT_REFLEX_ENABLE | RT_REFRACT_ENABLE;
				Core::TBVH tbvh{ MAX_OBJECTS };
				Core::SimpleComputeShader* rt_di_shader = nullptr;
				Core::SimpleComputeShader* rt_di_denoiser = nullptr;

				Core::SimpleComputeShader* gi_shader = nullptr;
				Core::SimpleComputeShader* gi_average = nullptr;
				Core::SimpleComputeShader* gi_weights = nullptr;

				static constexpr int RT_TEXTURE_REFLEX = 0;
				static constexpr int RT_TEXTURE_REFRACT = 1;
				static constexpr int RT_TEXTURE_EMISSION = 2;

				static constexpr int RT_NTEXTURES = 3;
				Core::RenderTexture2D rt_textures_di[2][RT_NTEXTURES];

				static constexpr int RT_GI_NTEXTURES = 5;
				Core::RenderTexture2D rt_textures_gi[RT_GI_NTEXTURES];
				Core::RenderTexture2D rt_textures_gi_tiles;

				Core::RenderTexture2D restir_pdf[2];
				Core::RenderTexture2D restir_w;
				Core::RenderTexture2D* restir_pdf_curr = nullptr;
				Core::RenderTexture2D* restir_pdf_prev = nullptr;

				static constexpr uint32_t RESTIR_HALF_KERNEL = 5;
				static constexpr uint32_t RESTIR_KERNEL = 2 * RESTIR_HALF_KERNEL + 1;
				static constexpr uint32_t RESTIR_PIXEL_RAYS = 16;
				static constexpr uint32_t RESTIR_TOTAL_RAYS = RESTIR_PIXEL_RAYS * RESTIR_KERNEL * RESTIR_KERNEL;


				Core::RenderTexture2D* rt_texture_di_prev;
				Core::RenderTexture2D* rt_texture_di_curr;
				Core::RenderTexture2D* rt_texture_gi_prev;
				Core::RenderTexture2D* rt_texture_gi_curr;
				Core::RenderTexture2D* rt_texture_gi_tmp[2];
				Core::RenderTexture2D* rt_texture_gi_trace = nullptr;

				Core::RenderTexture2D rt_ray_sources0;
				Core::RenderTexture2D rt_ray_sources1;
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

				//Copy texture shader
				Core::SimpleComputeShader* copy_texture = nullptr;

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

				//Lens/film artifact stage, found in the post-process chain the same way
				//dof_effect is. Null when the application installed no such stage, in
				//which case the settings below are simply remembered and never used.
				Core::LensEffect* lens_effect = nullptr;
				bool lens_enabled = true;
				float lens_aberration = 0.0f;
				float lens_grain = 0.0f;
				float lens_vignette = 0.0f;

				//Depth of field autofocus. AutoFocusCS reads the scene depth at the
				//center of the view out of depth_map (which stores world distance
				//from the camera) and keeps the smoothed focal distance in this 1x1
				//texture, which the DOF and lens flare shaders sample. The whole
				//measurement stays on the GPU - nothing is read back to the CPU.
				Core::RenderTexture2D autofocus_map;
				Core::SimpleComputeShader* autofocus_shader = nullptr;
				bool dof_autofocus = true;
				//Set for the first dispatch (and whenever autofocus is re-enabled) so
				//the measurement is adopted immediately instead of being smoothed in
				//from a stale distance.
				bool autofocus_reset = true;

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
				//The view-projection the *previous rendered frame* used, which is what
				//every temporal pass (motion vectors, the GI and RT denoisers) means by
				//"previous". Owned here rather than by Components::Camera because only the
				//renderer knows where a frame boundary is - CameraSystem ticks on the
				//background thread, at a rate unrelated to the render rate.
				float4x4 prev_view_projection{};
				bool prev_view_projection_valid = false;
				Core::SimpleComputeShader* motion_shader = nullptr;

				float time = 0.0f;
				bool tess_enabled = true;
				bool normal_material_map = true;
				bool normal_mesh_map = true;
				bool wireframe_enabled = false;
				bool aa_enabled = true;
				bool motion_blur_enabled = true;
				bool dof_enabled = true;
				bool scene_enabled = true;
				//Packed buffer selection + bypass flags, see eDebugBuffer.
				uint32_t rt_debug = 0;
				//Exposure applied to a debug buffer view. The colour buffers are HDR and
				//indirect light in particular sits well under 1.0, so raw it reads as
				//black; without a gain half the views look broken rather than dark.
				float debug_gain = 1.0f;
				uint32_t frame_count = 0;
				uint32_t current = 0;
				uint32_t prev = 1;

				bool cloud_test = false;

				void DrawSky(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection);
				void CastShadows(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, bool static_shadows);
				//Cheap fingerprint of everything the static shadow map depends on: which
				//entities are static casters and where they sit. The static map is only
				//re-rendered every STATIC_SHADOW_REFRESH_PERIOD frames, which is far too
				//coarse to react to an edit - toggling "Static" or dragging a static object
				//would otherwise leave a stale (or missing) shadow on screen for the rest of
				//the period, which reads exactly like static shadows being broken. Comparing
				//this per frame costs a walk over the shadow tree, cheap next to redrawing it.
				//Not const: EntityVector::GetData() is a non-const accessor.
				uint64_t StaticShadowSignature();
				uint64_t last_static_shadow_signature = 0;
				void DrawDepth(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection);
				void DrawScene(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection,
					ID3D11ShaderResourceView* prev_pass_texture,
					Core::IRenderTarget* target, RenderTree& tree);

				void LoadRTResources();
				void ResetRTBBuffers();
				void CopyTexture(const Core::RenderTexture2D& input, Core::RenderTexture2D& output);
				void ProcessMotion();
				void PrepareRT();
				void ProcessRT();
				void ProcessGI();
				void ProcessDust();
				void ProcessLensFlare();
				void ProcessMotionBlur();
				void ProcessMix();
				void ProcessAntiAlias();
				void ProcessAutoFocus();

				void DrawParticles(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection, RenderParticleTree& tree);
				bool IsVisible(const float3& camera_pos, const DrawableEntity& drawable, const matrix& view_projection, int w, int h) const;
				void CheckSceneVisibility(RenderTree& tree);
				//Everything a motion vector is measured against: the camera's
				//view-projection, every entity's world matrix and every skinned mesh's pose,
				//stored as "previous" for the next frame. Called at the very end of Draw, once
				//per rendered frame, and this is the only place any of the three is written -
				//see Transform::prev_world_matrix.
				void LatchPreviousFrame(const Components::Camera& camera);
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
				// Pulls an entity out of every draw tree and registers it again from its
				// current components.
				//
				// Needed when a material's *shaders* change: the trees are keyed by shader
				// tuple, and AddDrawable's own cleanup only evicts buckets whose material
				// differs - so re-adding under a new shader key while the material stays
				// the same leaves the old entry in place and the entity draws twice, once
				// with each shader. Assigning a different material does not need this
				// (that cleanup covers it); changing shaders in place does.
				void RefreshDrawable(ECS::Entity entity);

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
				//Depth of field autofocus (on by default). The focal distance is
				//measured every frame on the GPU as the scene depth at the center of
				//the view - the distance from the camera to whatever it is aimed at -
				//and smoothed over time so it does not snap as the view sweeps across
				//near geometry. Turn it off to drive the distance manually through
				//BaseDOFProcess::SetFocus. The aperture (SetAmplitude) is never
				//touched by autofocus.
				void SetDofAutofocus(bool enabled);
				bool GetDofAutofocus() const;

				// Physical camera artifacts, applied by the Core::LensEffect stage of
				// the post-process chain (nothing happens if the application did not
				// install one). Each amount is 0 (off) to 1 (strongest), and values
				// outside that range are clamped:
				//  - aberration: radial colour fringing, zero at the center of the
				//    frame and growing towards the corners, as a real lens disperses.
				//  - grain: animated monochrome film grain, weighted towards the
				//    darker parts of the image the way emulsion grain behaves.
				//  - vignette: brightness falloff towards the corners.
				// The master switch zeroes all three without disturbing them, so a
				// look can be toggled for comparison and switched back on intact.
				void SetLensEffects(bool enabled);
				bool GetLensEffects() const;
				void SetLensAberration(float amount);
				float GetLensAberration() const;
				void SetLensGrain(float amount);
				float GetLensGrain() const;
				void SetLensVignette(float amount);
				float GetLensVignette() const;
				// Render-target debugging. The packed form is what reaches the shaders;
				// the typed accessors are what callers should use.
				//
				// While a buffer view is on, the camera-artifact chain (AA, motion blur,
				// depth of field, lens) is suppressed for that frame - a vignetted,
				// depth-blurred normal buffer is not an inspection of anything. The
				// stored settings are untouched and come back when the view is turned
				// off, because they are pushed to the GPU fresh every frame anyway.
				void SetRTDebug(uint32_t debug);
				uint32_t GetRTDebug() const;
				void SetDebugBuffer(eDebugBuffer buffer);
				eDebugBuffer GetDebugBuffer() const;
				void SetDebugFlag(uint32_t flag, bool enabled);
				bool GetDebugFlag(uint32_t flag) const;
				// True while any buffer view is selected.
				bool IsDebugBufferActive() const;
				void SetDebugGain(float gain);
				float GetDebugGain() const;
				void SetSceneEnabled(bool enabled);
				bool GetSceneEnabled() const;
			};
		}
	}
}
