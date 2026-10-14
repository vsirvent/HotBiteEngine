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
#include <Core\RWBuffer.h>
#include <Core\TiledLights.h>
#include <Core\ShadowRays.h>

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
				//The nearest MAX_AUX_LIGHTS point lights, for the shaders with no screen tile to
				//read a mask for (ray hits, glows, the volumetric march). Surfaces are lit from
				//the tile masks instead, see Core::TiledLights.
				static const std::string AUX_LIGHT_COUNT;
				static const std::string AUX_LIGHTS;
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

				//What the last PrepareRT handed the ray tracers, in triangle indices
				//summed over the objects it sent (the MAX_OBJECTS best of them: see PrepareRT).
				//The only readout there is of the level of detail selection actually
				//reaching them: which geometry a ray hits is invisible in a screenshot
				//- a reflection tracing the wrong mesh still looks like a reflection -
				//and the timings do not show it either, since on a scene whose GI cost
				//is its denoiser, tracing a tenth of the triangles costs the same to
				//within noise.
				//
				//Written on the ray tracing thread and read wherever a tool asks: a
				//plain unsynchronized read of values that are only ever reported.
				struct RtGeometryStats {
					int objects = 0;
					uint64_t full_indices = 0;   //what tracing level 0 would have cost
					uint64_t traced_indices = 0; //what every ray actually walks
						//Which drawables make the cut is a matter of priority, not only of distance (see PrepareRT).
						//These say how many drawables in reach of the screen (and how many of them emit light) there
						//were and how many of those were left out, so a scene that lacks an object in its indirect
						//light can be told from one that is only dim.
						int candidates = 0;          //traceable drawables of the world
						int view_candidates = 0;     //... of them within a ray's reach of the screen
						int view_dropped = 0;        //... that did not fit in MAX_OBJECTS
						int emissive_candidates = 0; //... of them that emit light
						int emissive_dropped = 0;    //... that did not fit (stays 0 below the emissive cap)
				};

				//The world radiance cache's occupancy, read back off the GPU. The
				//cache is invisible in a screenshot - a ray reading a cell that does
				//not exist yet just produces the same one-bounce answer it always did -
				//so this is the only way to see it working at all.
				//
				//`deposits`/`dropped` are sampled from one thread per 8x8 group and
				//scaled by 64, so they are an estimate of the per-frame totals; the
				//three counted in the resolve pass are exact. Two frames stale by
				//construction (see Core::RWByteBuffer::Readback) - these are counters
				//for a readout, not a fence.
				struct RadianceCacheStats {
					uint32_t live = 0;      //cells holding a value
					uint32_t touched = 0;   //cells that received a sample this frame
					uint32_t evicted = 0;   //cells aged out this frame
					uint32_t deposits = 0;  //samples that found a slot
					uint32_t dropped = 0;   //samples lost to a full probe run
					uint32_t hits = 0;      //lookups that found a cell
					uint32_t misses = 0;    //lookups that did not
					uint32_t entries = 0;   //table size, so a caller can form a ratio
				};

				//What the tessellation stages did in the main scene pass, straight from the GPU's
				//pipeline statistics. Tessellation is nearly invisible in a screenshot in the ways
				//that matter: a surface tessellated at 16 and at 32 subdivisions differs by a few
				//pixels of relief, and a patch that came out at half the density of its neighbour
				//shows as, at best, a crack. The count of tessellated vertices (domain shader
				//invocations) is the only direct evidence of what the hull shader decided.
				//
				//Summed over every tessellated draw in the frame, so it is only meaningful as a
				//difference: the same scene before and after a change. Untessellated patches count
				//too (one vertex per control point), which is the constant baseline to subtract.
				//Two or three frames stale by construction (the query is read back without
				//stalling the GPU).
				struct TessStats {
					uint64_t hs_invocations = 0; //control points run through the hull shader
					uint64_t ds_invocations = 0; //tessellated vertices
					uint32_t frame = 0;          //the frame this describes
					bool valid = false;          //false until the first reading has come back
				};

				//What the splat binning did this frame, read back off the GPU. A splat
				//cloud that is over per-tile capacity does not fail visibly - it renders
				//a plausible surface with parts of it missing - so this is the only way
				//to tell "the capacity is enough" from "the capacity is not".
				//
				//`max_per_tile` is uncapped, so it is the number to size the capacity
				//against; `overflow_tiles` at anything other than 0 means splats were
				//lost. Two frames stale by construction (Core::RWByteBuffer::Readback).
				struct SplatStats {
					uint32_t tiles_used = 0;      //tiles holding at least one splat
					uint32_t max_per_tile = 0;    //entries in the deepest tile
					uint32_t total_binned = 0;    //(splat, tile) pairs that survived the cull
					uint32_t dropped = 0;         //entries lost because the pool was short
					uint32_t capacity = 0;        //pool size in entries, to form a ratio
					uint32_t tiles_rastered = 0;  //tiles the rasterizer actually ran over
					uint32_t pixels_written = 0;  //pixels it wrote
					//(pixel, entry) pairs the front-to-back walk examined, summed over
					//every thread. The ONLY evidence the transmittance early-out works:
					//a walk that stops at the opaque surface and one that reads every
					//entry render the same image by construction, so nothing in the
					//frame can distinguish them. Compare against tiles_rastered * 256 *
					//the average slice length to see how much of the slice is being
					//skipped - on a dense cloud most of it.
					uint32_t entries_walked = 0;
				};

				enum class eRtQuality {
					OFF,
					LOW,
					MID,
					HIGH
				};

				//Where a reflection or refraction ray is solved. SCREEN marches the depth buffer:
				//cheap, and it answers with the colour already drawn, but it only knows what is on
				//the screen, so a ray that leaves it or hides behind something is not answered.
				//WORLD walks the BVH of the best MAX_OBJECTS objects (emitters and what the screen sees first): it sees all of them, at the
				//price of a walk per ray. BOTH runs the screen first and hands the world only what
				//it left (packed, so the world's warps are full - see RayReflexScreenSolverCS).
				//Refractions only ever exist in the world: the back of an object is not on the
				//screen. The values are bits, and MUST match RT_SPACE_* in Shaders/RayTrace/RayLists.hlsli.
				enum class eRtSpace : uint32_t {
					SCREEN = 1,
					WORLD = 2,
					BOTH = 3
				};

				//What the reflection pass did last frame, read back off the GPU: of the pixels that
				//reflect, how many the screen answered and how many were left to trace in the world.
				//The picture cannot tell the two solvers apart (they are meant to agree), so this is
				//the only way to see the split. Stale by a few frames by construction, like the other
				//counters, and only collected while somebody asks (GetRayTracingStats).
				struct RtSpaceStats {
					uint32_t reflective = 0;    //pixels that trace a reflection
					uint32_t screen_solved = 0; //answered by the screen march
					uint32_t world_reflect = 0; //left to the world tracer
					uint32_t world_refract = 0; //refracting pixels the world tracer was given
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
					GI_CACHE,       //world radiance cache, looked up per visible pixel
					GI_CACHE_CONF,  //that cache's confidence, as a cold-to-hot ramp
					//The ray source pair (rt_ray_sources0/1): the two targets every ray
					//tracing pass reads to decide whether to trace from a pixel and with
					//what. POSITION and NORMAL above already show their xyz halves -
					//these show the four scalars packed into the two w channels, plus the
					//mask that combines them into the tracers' own accept/reject
					//decision. See DebugRayMaskColor in RenderDebug.hlsli.
					RAY_SOURCES,     //which pixels trace, and why the rest do not
					RAY_DISPERSION,  //surface roughness; >=1 means no reflection ray
					RAY_REFLEX,      //MaterialProps::rt_reflex; 0 means no ray at all
					RAY_DENSITY,     //refraction index, for the refracted ray
					RAY_OPACITY,     //below 1 is what spawns a refraction ray
					//The geometry the ray traced shadows are traced through (the objects of the
					//scenes in reach of the camera, each at the level of detail its rays hit),
					//seen from the camera. SHADOW_BVH colours each triangle by its size in world
					//units (red tiny, blue large: density), SHADOW_BVH_COST by how many nodes of
					//the hierarchies the camera ray walked (the cost of a ray there).
					SHADOW_BVH,
					SHADOW_BVH_COST,
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

				//A Gaussian splat cloud. Deliberately NOT a DrawableEntity: it carries no
				//Mesh and no Material, it is in none of the render trees (which are keyed
				//by shader tuple, and this one has no raster shaders at all), and it is
				//drawn by a compute pass of its own rather than by a draw call. The only
				//things it shares with a mesh entity are the transform and Base::visible.
				struct SplatEntity {
					Components::Base* base = nullptr;
					Components::Transform* transform = nullptr;
					Components::SplatCloud* cloud = nullptr;
					SplatEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						transform = &(c->GetComponent<Components::Transform>(entity));
						cloud = &(c->GetComponent<Components::SplatCloud>(entity));
					}
				};
				ECS::Signature splat_signature;

				struct SkyEntity : public DrawableEntity {
					Components::Sky* sky;
					SkyEntity(ECS::Coordinator* c, ECS::Entity entity) : DrawableEntity(c, entity) {
						sky = &(c->GetComponent<Components::Sky>(entity));
					}
				};
				ECS::Signature sky_signature;

				struct AmbientLightEntity {
					Components::Base* base;
					Components::AmbientLight* light;
					AmbientLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						light = &(c->GetComponent<Components::AmbientLight>(entity));
					}
				};
				ECS::Signature amblight_signature;

				struct DirectionalLightEntity {
					Components::Base* base;
					Components::DirectionalLight* light;
					DirectionalLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
						light = &(c->GetComponent<Components::DirectionalLight>(entity));
					}
				};
				ECS::Signature dirlight_signature;

				struct PointLightEntity {
					Components::Base* base;
					Components::Transform* transform;
					Components::PointLight* light;
					PointLightEntity(ECS::Coordinator* c, ECS::Entity entity) {
						base = &(c->GetComponent<Components::Base>(entity));
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
				ECS::EntityVector<SplatEntity> splat_clouds;
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

				//Binds the ambient and directional lights, their shadow maps, and the nearest
				//point lights in the aux constant buffer (`volumetric` picks the nearest of the
				//lights that have a fog density instead). Surface shading does not go through
				//this: it reads the tile masks.
				void PrepareLights(Core::ISimpleShader* s, bool volumetric = false);
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
				//The G-buffer's eighth target: how each surface takes light (spec, bloom, the
				//shading model, ...), written by the geometry passes and read by the deferred
				//lighting. Layout and meaning per model in Shaders/Common/DeferredLighting.hlsli.
				//UNORDERED_ACCESS so SplatRasterCS can write it from compute.
				Core::RenderTexture2D material_map;
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

				//Ray tracing. These two MUST agree: SetRayTracingQuality only touches
				//the divider when the quality actually *changes*, so a mismatched pair
				//here is a state the engine can never be talked out of - it said MID
				//while tracing at the full-resolution divider of HIGH, and setting MID
				//(what a game does when it applies a saved option) was a no-op that left
				//it there. The reflection/refraction pass is the most expensive thing in
				//a ray traced frame and scales with this divider, so that mismatch was
				//worth about 2x: on Marbles' sponza, divider 1 is 27 fps and divider 3 is
				//52. HIGH is the pair that matches the behaviour every scene was authored
				//against; changing the default means changing what every game looks like.
				eRtQuality rt_quality = eRtQuality::HIGH;
				uint32_t RT_TEXTURE_RESOLUTION_DIVIDER = 1;
				static constexpr uint32_t RT_REFLEX_ENABLE = 1;
				static constexpr uint32_t RT_REFRACT_ENABLE = 2;
				static constexpr uint32_t RT_INDIRECT_ENABLE = 4;

				uint32_t rt_enabled = RT_INDIRECT_ENABLE | RT_REFLEX_ENABLE | RT_REFRACT_ENABLE;
				Core::TBVH tbvh{ MAX_OBJECTS };
				Core::SimpleComputeShader* rt_di_shader = nullptr;
				Core::SimpleComputeShader* rt_di_denoiser = nullptr;

				//Reflections and refractions in two steps. The screen solver goes first, marching
				//what is on the screen and listing, per kind of ray, the pixels it could not answer;
				//RayListArgsCS turns the lists' lengths into the sizes of the dispatches, and
				//rt_di_shader (the BVH walk) then runs over the lists and nothing else, so its warps
				//are full of rays that need it.
				eRtSpace rt_space = eRtSpace::WORLD;
				Core::SimpleComputeShader* ray_reflex_screen_solver = nullptr;
				Core::SimpleComputeShader* ray_list_args = nullptr;
				//Level 0 is depth_map; each of the others is the min of 5x5 of the one below.
				//HIZ_NTEXTURES MUST match HIZ_TEXTURES in RayScreenSolver.hlsli.
				Core::SimpleComputeShader* high_z_shader = nullptr;
				static constexpr uint32_t HIZ_RATIO = 5;
				static constexpr uint32_t HIZ_DOWNSAMPLED_NTEXTURES = 3;
				static constexpr uint32_t HIZ_NTEXTURES = HIZ_DOWNSAMPLED_NTEXTURES + 1;
				Core::RenderTexture2D high_z_downsampled_map[HIZ_DOWNSAMPLED_NTEXTURES];
				ID3D11ShaderResourceView* high_z_maps[HIZ_NTEXTURES]{};
				//How far along a ray, in world units, the screen march looks.
				static constexpr float RT_SCREEN_MAX_DISTANCE = 100.0f;
				//Its budget of steps per ray, by who is going to answer the rays it gives up on.
				static constexpr uint32_t RT_SCREEN_STEPS_WITH_WORLD = 96;
				static constexpr uint32_t RT_SCREEN_STEPS_ALONE = 256;
				uint32_t rt_screen_steps = 0;
				//The ray traced lights' shadows, solved on the screen first. One ray per lit pixel per
				//light, so the budget is a good deal smaller than a reflection's: a shadow ray goes
				//toward a light that is usually above what it starts on, and is clear of its own
				//surface in a few steps.
				eRtSpace shadow_space = eRtSpace::WORLD;
				static constexpr int SHADOW_SCREEN_STEPS = 32;
				static constexpr float SHADOW_SCREEN_DISTANCE = 60.0f;
				static constexpr float SHADOW_SCREEN_BIAS = 0.03f;
				static constexpr float SHADOW_SCREEN_THICKNESS = 0.7f;
				static constexpr float SHADOW_SCREEN_THICKNESS_REL = 0.02f;
				//Whether ProcessHighZ built the pyramid this frame: the solvers that read it check.
				bool high_z_valid = false;
				//The lists of rays for the world tracer (two, one capacity apart: reflection and
				//refraction), their counters (RT_COUNT_* in RayLists.hlsli) and the arguments of the
				//two indirect dispatches that walk them. Sized to the DI textures.
				static constexpr uint32_t RT_LIST_COUNTERS = 4;
				Core::RWTypedBuffer rt_ray_list;
				Core::RWByteBuffer rt_ray_counts;
				Core::RWTypedBuffer rt_ray_args;
				uint32_t rt_ray_list_capacity = 0;
				RtSpaceStats rt_stats_cpu;
				mutable uint32_t rt_stats_request_frame = 0;
				mutable bool rt_stats_requested = false;
				static constexpr uint32_t RT_STATS_KEEPALIVE = 120;

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

				//The world radiance cache. Sizes and the record layout are in
				//Shaders/Common/RadianceCache.hlsli and MUST stay in step with
				//RADIANCE_CACHE_ENTRIES below - the buffers are raw, so nothing
				//checks the stride and a mismatch reads neighbouring cells as this
				//one's colour.
				static constexpr uint32_t RADIANCE_CACHE_ENTRIES = 1048576;
				static constexpr uint32_t RADIANCE_CACHE_STRIDE = 32;
				static constexpr uint32_t RADIANCE_CACHE_VALUE_STRIDE = 16;
				static constexpr uint32_t RADIANCE_CACHE_STATS_BYTES = 32;
				Core::RWByteBuffer rcache;
				Core::RWByteBuffer rcache_value;
				Core::RWByteBuffer rcache_stats;
				Core::SimpleComputeShader* rcache_resolve = nullptr;
				RadianceCacheStats rcache_stats_cpu;
				//Frame of the last GetRadianceCacheStats call. The readback is a
				//CopyResource plus a driver Map every frame it runs, on the render
				//thread, for counters nothing in the frame depends on - so it runs only
				//while something is actually reading them, and stops on its own a couple
				//of seconds after the last request.
				mutable uint32_t rcache_stats_request_frame = 0;
				mutable bool rcache_stats_requested = false;
				static constexpr uint32_t RADIANCE_CACHE_STATS_KEEPALIVE = 120;
				//Cleared on the next resolve. A level load leaves the table full of
				//cells describing geometry that no longer exists, and those would keep
				//answering lookups for RC_MAX_AGE frames of the new level.
				bool rcache_reset = true;

				static constexpr uint32_t RESTIR_HALF_KERNEL = 5;
				static constexpr uint32_t RESTIR_KERNEL = 2 * RESTIR_HALF_KERNEL + 1;
				static constexpr uint32_t RESTIR_PIXEL_RAYS = 16;
				static constexpr uint32_t RESTIR_TOTAL_RAYS = RESTIR_PIXEL_RAYS * RESTIR_KERNEL * RESTIR_KERNEL;


				//Never default-initialized before: harmless while every caller happened
				//to construct RenderSystem on fresh (so OS-zeroed) heap memory, but that
				//is not something the type should rely on - see the null-guards in
				//ProcessMix, which is exactly the code that was silently depending on it.
				Core::RenderTexture2D* rt_texture_di_prev = nullptr;
				Core::RenderTexture2D* rt_texture_di_curr = nullptr;
				Core::RenderTexture2D* rt_texture_gi_prev = nullptr;
				Core::RenderTexture2D* rt_texture_gi_curr = nullptr;
				Core::RenderTexture2D* rt_texture_gi_tmp[2] = { nullptr, nullptr };
				Core::RenderTexture2D* rt_texture_gi_trace = nullptr;

				Core::RenderTexture2D rt_ray_sources0;
				Core::RenderTexture2D rt_ray_sources1;
				Core::ExtBVHBuffer tbvh_buffer;
				Core::BVHBuffer* bvh_buffer = nullptr;
				//The ray tracers' view of the scene, filled by PrepareRT: the nearest
				//MAX_OBJECTS drawables, each pointing at the **coarsest level of detail
				//its mesh has**. Reflections, refractions and indirect light all read
				//this one array - none of them wants the detail, and this pass is the
				//most expensive thing in a ray traced frame.
				//
				//It used to be two arrays, with reflections tracing the level being
				//*drawn* (floored by the quality setting) and only the GI taking the
				//coarsest. That distinction bought a silhouette nobody can see through a
				//half-resolution, denoised reflection, and cost a second 20 KB cbuffer
				//upload per frame.
				ObjectInfo objects[MAX_OBJECTS]{};
				MaterialProps objectMaterials[MAX_OBJECTS]{};
				int nobjects = 0;
				RtGeometryStats rt_geometry_stats;

			public:
				struct InstancingStats {
					uint32_t draws = 0;
					uint32_t entities = 0;
				};
				struct GpuTimings {
					double shadows = 0.0;
					double depth = 0.0;
					double scene = 0.0;
					//Everything after the scene up to the post-process chain: the sum of the seven below.
					double post = 0.0;
					double motion = 0.0;
					double rt = 0.0;
					double gi = 0.0;
					double light = 0.0;
					//Inside `scene`, not on top of it: the tile culling and the deferred lighting
					//dispatch that run between the geometry and the splats, measured for the first
					//of the frame's lighting passes (a frame with a second pass or splat clouds runs
					//another, which costs more than is reported here).
					double tile_cull = 0.0;
					double deferred = 0.0;
					double mix = 0.0;
					double aa = 0.0;
					double blur = 0.0;
					uint32_t frames = 0;
				};
				//What the CPU spent on each part of a frame's Draw, in milliseconds summed over rames frames: the
				//per-entity passes over the world (visibility, level of detail, the static-shadow signature), the shadow
				//maps (the static one only on the frames it is redrawn), the depth pre-pass, the scene, what follows it up
				//to the post-process chain, and the latch of the previous frame. Always on (a handful of clock reads).
				struct CpuTimings {
					double visibility = 0.0;
					double lods = 0.0;
					//Choosing and packing the frame's point lights (GatherLights).
					double lights = 0.0;
					double signature = 0.0;
					double static_shadows = 0.0;
					double shadows = 0.0;
					double depth = 0.0;
					double scene = 0.0;
					double post = 0.0;
					double latch = 0.0;
					uint64_t frames = 0;
				};
			private:
				//Instanced draws (SetInstancing). The matrices of the instances of a draw, as the vertex shaders
				//read them (Core/Shaders/Common/Instancing.hlsli): where the entity stands, and where it stood
				//in the previous rendered frame.
				struct InstanceData {
					float4x4 world;
					float4x4 prev_world;
				};
				enum class InstancePass { Scene, Depth, Shadow };
				//One entity waiting to be grouped: the geometry its draw would use, and the entity.
				struct InstanceRef {
					const Core::MeshData* data;
					size_t index_count;
					size_t index_offset;
					size_t vertex_offset;
					DrawableEntity* drawable;
					bool SameGeometry(const InstanceRef& o) const {
						return data == o.data && index_count == o.index_count && index_offset == o.index_offset && vertex_offset == o.vertex_offset;
					}
					bool operator<(const InstanceRef& o) const {
						if (data != o.data) { return data < o.data; }
						if (index_count != o.index_count) { return index_count < o.index_count; }
						if (index_offset != o.index_offset) { return index_offset < o.index_offset; }
						return vertex_offset < o.vertex_offset;
					}
				};
				//Fewer than this many alike entities are drawn one by one: a group is not worth its buffer.
				static constexpr size_t MIN_INSTANCES = 4;
				bool instancing_enabled = true;
				uint32_t instancing_passes = 7;
				ID3D11Buffer* instance_buffer = nullptr;
				ID3D11ShaderResourceView* instance_view = nullptr;
				uint32_t instance_capacity = 0;
				std::vector<InstanceRef> instance_pending;
				std::vector<InstanceData> instance_data;
				InstancingStats instancing_stats_frame;
				InstancingStats instancing_stats_last;
				bool EnsureInstanceBuffer(uint32_t count);
				void ReleaseInstanceBuffer();
				//Whether the shaders a bucket draws with are the engine's own (the ones the instanced vertex shaders
				//are written for) and its material allows it, for each pass.
				bool CanInstanceBucket(InstancePass pass, const Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs,
					Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				static bool CanInstanceEntity(const DrawableEntity& entity);
				//Draws the entities `accept` lets through, in groups where they are alike.
				template <class Accept>
				void DrawBucket(ECS::EntityVector<DrawableEntity>& bucket, InstancePass pass, bool can_instance, Core::SimpleVertexShader* vs,
					Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps, Accept&& accept);
				static constexpr int GPU_RING = 4;
				static constexpr int GPU_STAMPS = 14;
				struct GpuFrame {
					ID3D11Query* disjoint = nullptr;
					ID3D11Query* stamp[GPU_STAMPS] = {};
					bool pending = false;
				};
				GpuFrame gpu_ring[GPU_RING];
				int gpu_next = 0;
				GpuFrame* gpu_open = nullptr;
				GpuTimings gpu_timings;
				CpuTimings cpu_timings;
				mutable bool gpu_requested = false;
				mutable uint32_t gpu_requested_frame = 0;
				void GpuBegin();
				void GpuStamp(int index);
				void GpuEnd();
				void GpuResolve();
				void GpuRelease();
				void DrawInstanceGroup(InstancePass pass, const InstanceRef* first, size_t count, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs,
					Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

				//A ring of pipeline-statistics queries around the main scene pass. It runs only
				//while something is reading it (GetTessStats), like the radiance cache counters:
				//a query and its readback are not free and nothing in the frame depends on them.
				static constexpr int TESS_QUERY_RING = 6;
				static constexpr uint32_t TESS_STATS_KEEPALIVE = 120;
				ID3D11Query* tess_query[TESS_QUERY_RING] = {};
				uint32_t tess_query_frame[TESS_QUERY_RING] = {};
				bool tess_query_pending[TESS_QUERY_RING] = {};
				int tess_query_next = 0;
				TessStats tess_stats_cpu;
				mutable uint32_t tess_stats_request_frame = 0;
				mutable bool tess_stats_requested = false;
				ID3D11Query* BeginTessStats();
				void EndTessStats(ID3D11Query* query);
				void ResolveTessStats();
				ID3D11ShaderResourceView* diffuseTextures[MAX_OBJECTS]{};
				std::mutex rt_mutex;
				std::condition_variable rt_signal;
				bool rt_end = false;
				bool rt_prepare = false;
				std::thread rt_thread;

				//Gaussian splat clouds, in five dispatches per visible cloud:
				//
				//  SplatPreprocessCS  project every Gaussian; record how near it is in
				//                     each tile it covers
				//  SplatBinCS  pass 0 histogram of (tile, depth bucket), culling what is
				//                     too far behind a tile's own nearest splat
				//  SplatScanCS        scan each tile's buckets -> offsets within the tile
				//  SplatBaseCS        scan the per-tile totals -> each tile's base in the
				//                     shared entry pool
				//  SplatBinCS  pass 1 scatter the entries into their slots
				//  SplatRasterCS      one group per tile, over that tile's contiguous
				//                     slice, into the targets MainRenderPS fills
				//
				//The histogram-scan-scatter replaced a fixed per-tile capacity plus a
				//bitonic sort. It costs one extra pass over the splats and removes both:
				//a tile takes exactly the room it needs, and its slice comes out ordered
				//front to back at bucket granularity as a side effect of the layout.
				Core::SimpleComputeShader* splat_preprocess = nullptr;
				Core::SimpleComputeShader* splat_bin = nullptr;
				Core::SimpleComputeShader* splat_compact = nullptr;
				Core::SimpleComputeShader* splat_scan = nullptr;
				Core::SimpleComputeShader* splat_base = nullptr;
				Core::SimpleComputeShader* splat_raster = nullptr;

				//Mirrors `SplatView` in Shaders/Splats/SplatCommon.hlsli field for field.
				//Never uploaded from here - only the *size* crosses the boundary, as the
				//structured buffer's stride - but it has to be written out so that stride
				//is derived from the layout instead of typed in as a number.
				struct SplatView {
					float2 screen_xy;
					float3 conic;
					float view_depth;
					float3 albedo;
					float3 normal;
					float alpha;
					float spec;
					float radius;
				};
				//What fxc computed for the HLSL struct: `dcl_resource_structured t20, 60`.
				//float3/float2 are 12 and 8 bytes here - the alignas in their typedefs is
				//a documented no-op (see Defines.h) - so the C++ layout packs the same way
				//fxc does, and this assert is what keeps that true.
				static_assert(sizeof(SplatView) == 60,
					"SplatView must match the 60-byte stride SplatCommon.hlsli is compiled with");

				//All of these mirror SplatCommon.hlsli. There is deliberately no per-tile
				//capacity among them any more: a tile's entries live in a contiguous
				//slice of one shared pool, at an offset the scan passes hand it.
				static constexpr uint32_t SPLAT_TILE_SIZE = 16;
				//Threads per group in SplatPreprocessCS and SplatBinCS, one splat each.
				static constexpr uint32_t SPLAT_PREPROCESS_GROUP = 256;
				//Depth bands a tile's slice is ordered into, which is the precision of
				//the counting sort the binning performs - see the note in
				//SplatCommon.hlsli. At 1024 over a span of SPLAT_MAX_DEPTH_STEP the
				//bucket index IS the quantized depth, so the slice is exactly sorted
				//and two entries sharing a bucket share a depth. That is a requirement
				//now rather than a luxury: SplatRasterCS composites with transmittance,
				//which is order-dependent.
				//
				//Costs tiles * this uints, cleared per cloud per frame - 7.1 MB at
				//1080p, 57 MB at 2560x1377 - and pins SplatScanCS at the 1024-thread
				//D3D11 group cap, since that dispatch is one thread per bucket.
				static constexpr uint32_t SPLAT_DEPTH_BUCKETS = 1024;
				static constexpr uint32_t SPLAT_SCAN_GROUP = 256;
				//Threads per group in SplatCompactCS, one tile each. Must match
				//SPLAT_COMPACT_GROUP in SplatCommon.hlsli.
				static constexpr uint32_t SPLAT_COMPACT_GROUP = 256;
				static constexpr uint32_t SPLAT_MAX_DEPTH_STEP = (1u << 10) - 1u;
				//tile_depth is cleared to this, so the first splat to touch a tile wins
				//the InterlockedMin and a tile nothing touches rejects everything.
				static constexpr uint32_t SPLAT_NO_DEPTH = 0xFFFFFFFFu;
				//How far past the surface the rasterizer keeps gathering, in WORLD UNITS
				//and not as a fraction of anything. A surface is a surface: the splats
				//making up the one covering a pixel are spread over a couple of
				//centimetres whether they were captured as part of an octopus or as part
				//of a room, so the tail that collects them is a fixed thickness. Scaling
				//it by the cloud's own depth extent - which is what this was - makes the
				//tail grow with the size of the capture for no reason connected to what it
				//is measuring: on a 1-unit object 5% is 0.05 and about right, on a 20-unit
				//room scan the same 5% is a metre, which averages whole pieces of
				//furniture into one pixel.
				//
				//This is a tail, not a search window, and that distinction is what fixed
				//the seams. It used to be measured from the nearest splat with any
				//coverage, which made it do two incompatible jobs: wide enough to reach
				//the surface *behind* a depth jump (or the pixel wrote nothing and the
				//background showed through as a 1-2 px seam along every overlap), yet
				//narrow enough not to average front and back together everywhere else.
				//The surface is found by a coverage threshold now (SplatRasterCS), and
				//the tail is applied only *after* one has been found, so no value here can
				//stop a pixel finding a surface - which is what makes it safe to be this
				//tight. What is left for it to do is collect the rest of the splats of the
				//surface already found, and anything past that is a different surface:
				//averaged in, it is what makes a cloud read as semi-transparent and drags
				//the depth it writes behind where the object is.
				//
				//It may not go to zero, however tempting the simplification looks - the
				//header of SplatRasterCS carries the A/B. This value also has a SECOND
				//consumer that has nothing to do with the tail: SplatPreprocessCS's coarse
				//reject of a splat behind the opaque depth, where it is the slack that
				//keeps a splat straddling a silhouette from being thrown away whole.
				static constexpr float SPLAT_SLAB_WORLD = 0.02f;

				//Floor for the entry pool, holding until the GPU has reported what it
				//actually needed. Entries per splat varies far too much for a multiplier:
				//a splat is binned into every tile its 3 sigma ellipse touches, so a
				//*small* cloud close up has the largest ellipses and the highest ratio
				//(the automation fixture's 600-splat sphere runs at ~120 entries a splat)
				//while a 1.8M capture at a normal distance sits under 2. So this is a
				//floor, the measurement does the sizing, and 256k entries costs 1 MB.
				static constexpr uint32_t SPLAT_POOL_MIN_ENTRIES = 262144;
				//Headroom over the measured requirement, so a pool that is exactly big
				//enough this frame does not have to grow again the moment the camera
				//moves a little closer.
				static constexpr float SPLAT_POOL_MARGIN = 1.25f;

				//The projected splats of the cloud currently being drawn, and the tile
				//structure pointing into them. One cloud at a time - each needs its own
				//world matrix, and the rasterizer consumes the bins before the next cloud
				//refills them - so these are sized for the *largest* cloud on screen
				//rather than for all of them, and grown on demand by EnsureSplatBuffers.
				Core::RWStructuredBuffer splat_views;
				//Per tile, the quantized depth of the nearest splat touching it. SplatBinCS
				//bins directly on each splat's own global quantized depth now (see the
				//note on SPLAT_DEPTH_BUCKETS in SplatCommon.hlsli), so this buffer's only
				//remaining reader is SplatCompactCS's occupancy test - a tile still
				//holding SPLAT_NO_DEPTH is exactly one no splat touched.
				Core::RWTypedBuffer splat_tile_depth;
				//tiles * SPLAT_DEPTH_BUCKETS. The histogram after pass 0, each bucket's
				//offset within its tile after the scan, and the scatter's cursors after
				//pass 1 - one array through all three lives, because they are the same
				//numbers being refined.
				Core::RWTypedBuffer splat_bucket_offsets;
				Core::RWTypedBuffer splat_tile_total;
				Core::RWTypedBuffer splat_tile_base;
				//The pool itself: one uint per (splat, tile) pair that survived the cull.
				//Sized by content rather than by tiles * capacity, which is the whole
				//point - covering the worst tile of a 1.8M splat capture the old way
				//would have cost 780 MB.
				Core::RWTypedBuffer splat_entries;
				//The tiles this cloud actually covers, densely packed, and the
				//(groups, 1, 1) argument that launches one group per entry of it.
				//SplatScanCS and SplatRasterCS are dispatched indirectly over these
				//instead of over the whole tile grid - see SplatCompactCS.
				Core::RWTypedBuffer splat_tile_list;
				Core::RWTypedBuffer splat_dispatch_args;
				Core::RWByteBuffer splat_stats;
				uint32_t splat_views_capacity = 0;
				uint32_t splat_entries_capacity = 0;
				uint32_t splat_tiles_x = 0;
				uint32_t splat_tiles_y = 0;

				//NOT keepalive-gated, unlike the radiance cache counters this is otherwise
				//modelled on. `total_binned` is what EnsureSplatBuffers sizes the entry
				//pool from, so it is load-bearing rather than diagnostic and has to be read
				//on every frame that draws a cloud - gating it meant the pool only grew
				//while a tool happened to be watching, which rendered the automation
				//fixture's cloud at 2.6% of its coverage.
				static constexpr uint32_t SPLAT_STATS_BYTES = 32;
				SplatStats splat_stats_cpu;

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

				//Tiled deferred lighting. Every point and spot light of the frame is packed into
				//one 1024 entry buffer (GatherLights); a compute pass records per 16x16 screen
				//tile which of them reach it (Core::TiledLights::Cull) and a second one shades the
				//G-buffer with only those (DeferredLightCS). The geometry passes evaluate no
				//light at all.
				Core::TiledLights tiled_lights;
				Core::SimpleComputeShader* deferred_light = nullptr;
				//What GatherLights chose this frame. `packed_lights` is the uploaded buffer;
				//the two aux lists are the nearest lights (all of them, and those with a
				//volumetric density) for the shaders that have no tile.
				std::vector<Core::PackedLight> packed_lights;
				std::vector<Core::AuxLight> aux_lights;
				std::vector<Core::AuxLight> aux_volumetric_lights;
				//The lights holding a PointShadowMapTexture slot, by slot. They are the only
				//point lights whose cube map is rendered.
				std::array<Components::PointLight*, MAX_LIGHTS> shadow_slot_lights = {};
				uint32_t shadow_slot_count = 0;
				//GatherLights scratch, kept so a frame allocates nothing.
				struct LightCandidate {
					float dist2;
					PointLightEntity* entity;
				};
				std::vector<LightCandidate> light_candidates;
				std::vector<LightCandidate> shadow_candidates;
				Core::TiledLightStats tiled_stats;
				//Ray traced point light shadows: the lights GatherLights flagged, and the scene
				//BuildShadowRays loaded for them (see Core::ShadowRays). `shadow_ray_scratch` is
				//the per-frame object list, kept so a frame allocates nothing.
				Core::ShadowRays shadow_rays;
				uint32_t raytraced_light_count = 0;
				struct ShadowRayCandidate {
					float distance;
					const DrawableEntity* drawable;
				};
				//The casters in reach of each ray traced light (and of the sun, last), by the order of
				//BuildShadowRays' reach list, and the objects of the scene being built.
				std::vector<std::vector<ShadowRayCandidate>> shadow_scene_candidates;
				std::vector<Core::ObjectInfo> shadow_ray_scratch;
				//The scene a ray traced directional light's rays go through (the point lights name theirs in
				//their packed flags).
				uint32_t shadow_ray_sun_scene = Core::ShadowRays::NO_SCENE;
				//Which level of detail of a mesh the rays hit: -1 is the coarsest the mesh has (the
				//default: a shadow ray is a yes or no per pixel per light, and the walk of the
				//hierarchy is the cost of a ray traced light), 0 the mesh itself, n the nth level of
				//its chain (the last one when it has fewer). A mesh with no chain is its own level.
				int shadow_ray_lod = -1;
			public:
				//What a ray traced light's own building (the entities it is part of) does to its rays: `NONE` the building
				//is left out of the light's scene and the light shines evenly out of it; `FULL` it is traced as the model
				//is, so its windows and gaps let the light out (the default); `PROXY` it is traced as the solid stand-in
				//every other model is, which closes the openings and can put the light out altogether.
				enum class eShadowRayOwner { NONE, FULL, PROXY };
			private:
				eShadowRayOwner shadow_ray_owner = eShadowRayOwner::FULL;
				//Whether the lights that ask for a ray traced shadow get one. Off, they light without
				//any shadow (they hold no map either): for measuring what the rays cost.
				bool raytraced_lights_enabled = true;
				//What the shadow rays did (the SR_STATS counters of DeferredLightCS), while something asks.
				Core::RWByteBuffer shadow_ray_counters;
				uint32_t shadow_ray_counters_cpu[8] = {};
				mutable uint32_t shadow_ray_counters_frame = 0;
				mutable bool shadow_ray_counters_requested = false;
				//Radius, in world units, of the ball round a light that its shadow rays aim into.
				//The aim changes every frame and every pixel, so the temporal anti aliasing
				//averages the hard shadows into a soft edge as wide as this ball. 0 = hard.
				float shadow_ray_radius = 1.0f;
				//Ray traced directional lights (the sun): how many there are this frame, how far from
				//the camera they are traced (the largest cascade shadow distance among them), how far a
				//ray looks for a caster (mirrored by SR_SUN_RAY_LENGTH in ShadowRays.hlsli) and the tangent
				//of the cone the rays aim into - the sun is half a degree wide, this is a soft 1.5.
				uint32_t raytraced_directional_count = 0;
				float shadow_ray_sun_distance = 0.0f;
				static constexpr float SHADOW_RAY_SUN_LENGTH = 120.0f;
				float shadow_ray_sun_spread = 0.026f;
				//The jitter makes one frame's shadow edge noise, so the light of the ray traced
				//lights is accumulated over the frames (ShadowAccumulateCS): `shadow_rt_light` is
				//this frame's, the two history textures hold the running average (one read, one
				//written, swapped every frame). `shadow_ray_blend` is the weight of the new frame.
				Core::RenderTexture2D shadow_rt_light;
				Core::RenderTexture2D shadow_rt_history[2];
				Core::SimpleComputeShader* shadow_accumulate = nullptr;
				//The shadow_bvh debug buffers: the picture (ShadowBvhDebugCS), and the scene it traces, which
				//BuildShadowRays makes while one is selected (everything that casts, within SHADOW_BVH_VIEW_RANGE).
				Core::SimpleComputeShader* shadow_bvh_debug = nullptr;
				Core::RenderTexture2D shadow_bvh_map;
				uint32_t shadow_bvh_scene = Core::ShadowRays::NO_SCENE;
				static constexpr float SHADOW_BVH_VIEW_RANGE = 300.0f;
				void ProcessShadowBvhDebug();
				int shadow_rt_history_read = 0;
				bool shadow_rt_history_valid = false;
				float shadow_ray_blend = 0.08f;
			public:
				//How soft ray traced shadows are (the radius of the ball the rays aim into, in world
				//units; 0 = hard) and how fast the result settles (the weight of the newest frame,
				//1 = no averaging, noisy). The defaults are what games should use; these are for
				//tuning and for tests.
				void SetShadowRaySettings(float radius, float blend) {
					shadow_ray_radius = radius < 0.0f ? 0.0f : radius;
					shadow_ray_blend = blend < 0.01f ? 0.01f : (blend > 1.0f ? 1.0f : blend);
				}
				float GetShadowRayRadius() const { return shadow_ray_radius; }
				void SetShadowRayLod(int lod) { shadow_ray_lod = lod < -1 ? -1 : lod; }
				int GetShadowRayLod() const { return shadow_ray_lod; }
				void SetRayTracedLights(bool enable) { raytraced_lights_enabled = enable; }
				void SetShadowRayOwner(eShadowRayOwner mode) { shadow_ray_owner = mode; }
				eShadowRayOwner GetShadowRayOwner() const { return shadow_ray_owner; }
				bool GetRayTracedLights() const { return raytraced_lights_enabled; }
				//What the shadow rays of the last frames did, per frame: rays that went on to the world, rays the screen
				//ended, steps of the top level hierarchy, objects entered, steps of an object's hierarchy, triangles
				//tested, rays the world ended, pixels that traced. Asking turns the counting on: poll.
				struct ShadowRayCounters {
					uint32_t rays = 0, screen_hits = 0, tlas_steps = 0, objects_entered = 0, object_steps = 0, triangles = 0, world_hits = 0, pixels = 0;
				};
				ShadowRayCounters GetShadowRayCounters() const {
					shadow_ray_counters_frame = frame_count;
					shadow_ray_counters_requested = true;
					const uint32_t* c = shadow_ray_counters_cpu;
					return { c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7] };
				}
				float GetShadowRayBlend() const { return shadow_ray_blend; }
			private:
				//Gathers the casters in reach of the ray traced lights and builds their scene.
				void BuildShadowRays();
				//Read back only while something asks (the same keepalive as the other counters).
				mutable uint32_t tiled_stats_request_frame = 0;
				mutable bool tiled_stats_requested = false;
				static constexpr uint32_t TILED_STATS_KEEPALIVE = 120;
				//Lighting dispatches so far this frame: the first one is the one that is timed.
				int lighting_passes_this_frame = 0;
				bool tile_culling_enabled = true;
				void GatherLights(const CameraEntity& camera);
				//Culls the lights against the G-buffer as it stands and shades the surfaces the
				//filter selects into the current light map: 0 everything, 1 only what the second
				//pass drew, 2 only Gaussian splats (see DeferredLightCS).
				void ProcessDeferredLighting(int pass_filter);

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
				//Multiplies the per-pixel motion vector before it is stepped along in
				//MotionBlurCS, so it scales both the blur's reach and its own distance
				//cap together - 1.0 leaves the shader's original tuning untouched.
				float motion_blur_scale = 1.0f;
				bool dof_enabled = true;
				bool scene_enabled = true;
				//Packed buffer selection + bypass flags, see eDebugBuffer.
				uint32_t rt_debug = 0;
				//Exposure applied to a debug buffer view. The colour buffers are HDR and
				//indirect light in particular sits well under 1.0, so raw it reads as
				//black; without a gain half the views look broken rather than dark.
				float debug_gain = 1.0f;
				//Ceiling on the confidence GIAverageCS places in its screen-space history
				//(0..1). 0 makes a still camera look like a moving one.
				float gi_max_confidence = 0.0f;
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

				//Draws every visible Gaussian splat cloud, after DrawScene and before
				//anything that consumes the frame's buffers.
				//
				//Placed there because it both reads and writes what DrawScene produced:
				//the preprocess pass rejects a splat behind the opaque depth, and the
				//rasterizer then composites over the scene colour, the light map and that
				//same depth. Running it earlier would test against a depth buffer the
				//geometry had not filled yet; running it after ProcessMotion/ProcessRT/
				//ProcessMix would leave the mixer building the frame out of buffers the
				//clouds had not reached.
				//
				//The scene colour is taken as the post-process pipeline's UAV - the same
				//texture DrawScene bound as an RTV, which is why this cannot run until the
				//render targets are unbound. With no pipeline installed there is no such
				//texture and the pass does nothing.
				void DrawSplats(int w, int h, const float3& camera_position, const matrix& view, const matrix& projection);
				//Sizes the splat scratch buffers for `splat_count` Gaussians and a
				//`tiles_x` by `tiles_y` screen. Grows only - a cloud smaller than the last
				//one reuses what is already there - and returns false if an allocation
				//failed, which is the pass's cue to skip the frame rather than dispatch
				//against a null UAV.
				bool EnsureSplatBuffers(uint32_t splat_count, uint32_t tiles_x, uint32_t tiles_y);

				void LoadRTResources();
				void ResetRTBBuffers();
				void CopyTexture(const Core::RenderTexture2D& input, Core::RenderTexture2D& output);
				void ProcessMotion();
				//The depth pyramid the screen solvers march through. Built from depth_map, so after
				//everything that writes it.
				void ProcessHighZ();
				//The solvers this frame's reflections can actually use, as RT_SPACE_* bits: the
				//screen one needs the lit frame it samples, and so a post-process pipeline to
				//render into (the one the mixer composes from).
				uint32_t EffectiveRtSpace() const;
				//The same for the shadows: whether the screen march is wanted by a ray traced light
				//that exists this frame.
				bool ShadowScreenWanted() const;
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
				//Picks the level of detail every drawable is drawn at this frame, from
				//the chain its mesh asset declares (Core::MeshData::lods).
				//
				//Once per frame, over `drawables`, and before anything draws - both
				//halves of that matter. Over the flat list rather than the trees because
				//an entity has one geometry per frame no matter how many buckets it sits
				//in, and the trees hold it once per shader tuple. Before anything draws
				//because the depth pre-pass records the surface the main pass is then
				//tested against: a switch between the two passes leaves the main pass
				//drawing a silhouette the recorded depth does not match, which is not a
				//pop but a hole.
				void SelectLods(const Components::Camera& camera);
				//The geometry the ray tracers should use for a mesh, at level `lod` of
				//its chain (clamped to what the chain has). Null is never returned for a
				//mesh that has data: an out-of-range level falls back to the full mesh,
				//the same way Components::Mesh::SetLod does.
				static const Core::MeshData* LodGeometry(Core::MeshData* data, int lod);
				//Everything a motion vector is measured against: the camera's
				//view-projection, every entity's world matrix and every skinned mesh's pose,
				//stored as "previous" for the next frame. Called at the very end of Draw, once
				//per rendered frame, and this is the only place any of the three is written -
				//see Transform::prev_world_matrix.
				void LatchPreviousFrame(const Components::Camera& camera);
				void PostProcessLight();
				
				void PrepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

				//Binds (and unbinds) the layer stack a material draws with. Takes the
				//MaterialData rather than a Material component because that is where a
				//stack lives - the draw trees are keyed by material, so one bucket has
				//exactly one stack no matter how many entities are in it.
				void PrepareMultiMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareMultiMaterial(Core::MaterialData* material, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);

				void PrepareEntity(DrawableEntity& entity, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				void UnprepareEntity(DrawableEntity& entity, Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps);
				// The one DrawIndexed every mesh draw pass (depth pre-pass, point/directional
				// shadow, main scene) already repeated identically - factored out so the one
				// place that needs to know about a mesh with its own vertex/index buffer
				// (Core::MeshData::InitOwned - one-off, never-reused geometry like a
				// procedural terrain chunk, as opposed to every other mesh's shared,
				// World-wide `vertex_buffer`) is this single function, not four copies of
				// it. An owned-buffer mesh's vertexOffset/indexOffset are always 0 (it is
				// the only mesh in its own buffer), so it needs its own IASetVertexBuffers/
				// IASetIndexBuffer bound first and the shared buffer restored after, or it
				// would draw whatever the shared buffer happens to have bound at offset 0.
				void DrawMeshEntity(Components::Mesh* mesh);
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

				RtGeometryStats GetRtGeometryStats() const { return rt_geometry_stats; }

				//Instancing: entities of one bucket that draw the same geometry and differ only in where they
				//stand (a forest: the same few tree meshes, thousands of times) are drawn with one instanced
				//draw call per pass, taking their matrices from a structured buffer, instead of one draw each
				//with the constant buffers of every shader stage uploaded again for each. Only static, opaque
				//meshes of the engine's own shaders are grouped (not skinned, tessellated, displaced, blended
				//or world-aligned ones, and not a material with its own shaders): everything else draws as
				//before. It is on by default; switching it off draws the frame the old way, which is what the
				//tests compare against.
				void SetInstancing(bool enabled) { instancing_enabled = enabled; }
				bool IsInstancing() const { return instancing_enabled; }
				//Which passes may instance: bit 0 the scene, bit 1 the depth pre-pass, bit 2 the shadow maps (all by default).
				//For finding which pass a difference between the two ways of drawing comes from.
				void SetInstancingPasses(uint32_t mask) { instancing_passes = mask; }
				uint32_t GetInstancingPasses() const { return instancing_passes; }
				//What the last complete frame drew that way, summed over its passes (depth, shadows, scene):
				//instanced draw calls, and the entities they stood for.
				InstancingStats GetInstancingStats() const { return instancing_stats_last; }

				//How long the GPU spent on each part of a frame, in milliseconds summed over `frames` frames (timestamp
				//queries read back a few frames late): the shadow maps, the depth pre-pass, the scene (main pass and
				//what is drawn with it) and everything after it up to the post-process chain. Asking is what turns
				//the queries on, as with the tessellation counters: the first call answers with zeros, and the values
				//are live a few frames later. Two reads some seconds apart give the average cost of each part. The GPU
				//runs behind the CPU, so a part waiting for the CPU to submit it reads long: this says where the GPU
				//was busy or starved, not what is expensive to submit.

				CpuTimings GetCpuTimings() const { return cpu_timings; }

				GpuTimings GetGpuTimings() const {
					gpu_requested_frame = frame_count;
					gpu_requested = true;
					return gpu_timings;
				}
				//Asking is what turns the readback on; the first call after a quiet
				//period therefore answers with whatever was last read (zeros at
				//startup) and the values become live a few frames later. Callers poll
				//- see Wait-CacheStat in the 21-gicache suite.
				//Asking turns the query on, as with the radiance cache counters: poll, and
				//compare readings a few frames after each change.
				TessStats GetTessStats() const {
					tess_stats_request_frame = frame_count;
					tess_stats_requested = true;
					return tess_stats_cpu;
				}
				RadianceCacheStats GetRadianceCacheStats() const {
					rcache_stats_request_frame = frame_count;
					rcache_stats_requested = true;
					return rcache_stats_cpu;
				}
				//Drop every cell. Call whenever the scene the cache describes is
				//replaced wholesale - a level load - rather than merely changed.
				void ResetRadianceCache() { rcache_reset = true; }

				//Always live while a cloud is being drawn - the readback these come from
				//also sizes the entry pool, so it is never switched off. Still a few
				//frames stale by construction (Core::RWByteBuffer::Readback), so a caller
				//that changes something and reads immediately sees the state before it.
				SplatStats GetSplatStats() const { return splat_stats_cpu; }

				//What the tiled lighting did: how many point lights the frame packed, how many
				//hold a shadow slot, and - read back off the GPU a few frames late - how many
				//tiles are lit and how many lights the busiest tile and the average lit tile
				//hold. The point of the design is that the last two stay small however many
				//lights there are, so this is the readout that shows it. Asking is what turns
				//the readback on, as with the other counters: poll, and expect `valid` false
				//until the first reading has landed.
				Core::TiledLightStats GetTiledLightStats() const {
					tiled_stats_request_frame = frame_count;
					tiled_stats_requested = true;
					return tiled_stats;
				}
				//Checks the masks against every visible pixel by brute force and returns the
				//count of (pixel, light) pairs the masks lost: 0 when the culling is right.
				//Stalls the GPU - for tests, not for frames. Call from the render thread (the
				//automation channel does), at a point where the G-buffer holds a frame.
				Core::TiledLightValidation ValidateTileLights();
				//The point lights in the packed buffer last frame, in buffer order: for tests.
				const std::vector<Core::PackedLight>& GetPackedLights() const { return packed_lights; }
				//Turns the per-tile light culling off: every light is then shaded at every pixel
				//that shows a surface, which is the brute-force answer. The image is the same -
				//a light out of range adds nothing - and the cost is what the culling saves,
				//which is why the tests flip it. On by default; not for games.
				void SetTileCulling(bool enabled) { tile_culling_enabled = enabled; }
				bool GetTileCulling() const { return tile_culling_enabled; }

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
				//Which solvers answer the reflections (and, for the world, the refractions): see
				//eRtSpace. WORLD, the default, is what the engine always did.
				void SetRayTracingSpace(eRtSpace space);
				eRtSpace GetRayTracingSpace() const;
				//How many steps the screen march may take for one ray before it gives up on it (the
				//world tracer, if on, takes over). 0, the default, is what the mode wants: the march
				//is a filter in front of the BVH walk when both are on, and is not worth more than
				//the walk it saves, but when it is all there is it may look further.
				void SetRayTracingScreenSteps(uint32_t steps);
				uint32_t GetRayTracingScreenSteps() const;
				//The same choice for the ray traced LIGHTS' shadows: SCREEN looks for a caster in the
				//depth buffer between a surface and its light, WORLD walks the BVH of the casters, BOTH
				//asks the screen first and the BVH when it finds nothing. What only the screen can do is
				//cast from a SKINNED mesh: the BVH holds a skinned mesh in its bind pose, which is not
				//where it stands, so it is left out; the depth buffer has it where it is. What only the
				//BVH can do is cast from what is off the screen, or behind what is on it. WORLD, the
				//default, is what the engine always did.
				void SetShadowRaySpace(eRtSpace space);
				eRtSpace GetShadowRaySpace() const;
				//Asking switches the counting on (a few atomics a group and a readback), as with the
				//radiance cache: poll, and compare readings a few frames after a change.
				RtSpaceStats GetRayTracingStats() const {
					rt_stats_request_frame = frame_count;
					rt_stats_requested = true;
					return rt_stats_cpu;
				}
				void SetDustEnabled(bool enabled);
				bool GetDustEnabled() const;
				void SetDustEffectArea(int32_t num_particles, const float3& area, const float3& offset);
				void SetLensFlare(bool enabled);
				bool GetLensFlare() const;
				void SetAA(bool enabled);
				bool GetAA() const;
				void SetMotionBlur(bool enabled);
				bool GetMotionBlur() const;
				//Scales the strength of the motion blur (1.0 = default tuning, 0 = no
				//blur, >1 exaggerates it). Independent of the enable flag so a saved
				//scale survives toggling the effect off and back on.
				void SetMotionBlurScale(float scale);
				float GetMotionBlurScale() const;
				//Ceiling on how much the indirect light trusts its own history, 0..1
				//(clamped). Camera motion is what normally lowers that trust, so at 1 a
				//still camera converges fully (least noise, sharpest) and looks different
				//from a moving one; at 0 every pixel is treated as moving, so the two look
				//alike at the price of a noisier still image. It is a look, so it is part
				//of a level's render settings and not a debug switch.
				void SetGIMaxConfidence(float confidence);
				float GetGIMaxConfidence() const;
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
