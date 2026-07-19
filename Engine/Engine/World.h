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

#include <Core/Json.h>
#include <reactphysics3d/reactphysics3d.h>

#include "Defines.h"
#include <memory>
#include <set>
#include <Loader\FBXLoader.h>
#include <ECS/Coordinator.h>
#include <ECS/ComponentRegistry.h>
#include <Systems\CameraSystem.h>
#include <Systems\DirectionalLightSystem.h>
#include <Systems\PhysicsSystem.h>
#include <Systems\PlayerSystem.h>
#include <Systems\PointLightSystem.h>
#include <Systems\RenderSystem.h>
#include <Systems\StaticMeshSystem.h>
#include <Systems\SkySystem.h>
#include <Systems\ParticleSystem.h>
#include <Systems\AnimationSystem.h>
#include <Systems\AudioSystem.h>
#include <Components\Base.h>
#include <Components\Camera.h>
#include <Components\Lights.h>
#include <Components\Physics.h>
#include <Core\DXCore.h>
#include <Core\Material.h>
#include <Core\Mesh.h>
#include <Core\Utils.h>
#include <ECS\Types.h>
#include <Network\Commons.h>

namespace HotBite {
	namespace Engine {
		class World: public ECS::IEventSender, public ECS::EventListener {
		public:
			static inline ECS::EventId EVENT_ID_MATERIALS_LOADED = ECS::GetEventId<World>(0x00);
			static inline ECS::EventId EVENT_ID_MESHES_LOADED = ECS::GetEventId<World>(0x01);
			static inline ECS::EventId EVENT_ID_SHAPES_LOADED = ECS::GetEventId<World>(0x02);
			static inline ECS::EventId EVENT_ID_TEMPLATES_LOADED = ECS::GetEventId<World>(0x03);
			static inline ECS::EventId EVENT_ID_UPDATE_MAIN = ECS::GetEventId<World>(0x04);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND = ECS::GetEventId<World>(0x05);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND2 = ECS::GetEventId<World>(0x06);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND3 = ECS::GetEventId<World>(0x07);
			static inline ECS::EventId EVENT_ID_UPDATE_PHYSICS = ECS::GetEventId<World>(0x08);

		protected:
			std::atomic<uint64_t> current_server_nsec = 0;
			std::atomic<uint64_t> current_background_thread_nsec = 0;
			std::atomic<uint64_t> current_physics_thread_nsec = 0;
			uint64_t physics_thread_period = 1000000000 / 60;
			uint64_t background_thread_period = 1000000000 / 60;
			uint64_t tick_period = 0;
			bool lockstep_init = false;
			bool lockstep_sync = false;
			std::atomic<bool> physics_paused = false;
			ECS::Event lockstep_tick_ev{ this, Network::LockStep::Command::EVENT_ID_NEW_SERVER_COMMAND };
			
			Core::VertexBuffer<Core::Vertex>* vertex_buffer = nullptr;
			BVHBuffer* bvh_buffer = nullptr;
			reactphysics3d::PhysicsWorld* phys_world = nullptr;
			Core::DXCore* dx_core;

			//Absolute path of assets location
			std::string path;
			std::unordered_map<std::string, std::set<ECS::Entity>> template_entities;
			//Clone entity name -> root source entity name, so Init() can resolve the
			//collision ShapeData of clones created during Load() (shapes are keyed by
			//the original FBX entity name).
			std::unordered_map<std::string, std::string> clone_shape_alias;
			std::unordered_map<std::string, nlohmann::json> multi_materials;
			// Components a level record explicitly stripped from an entity, by entity id.
			// Init() hands every mesh entity a Physics component when it has none, which
			// would silently undo a "remove": ["Physics"] the loader had just honoured -
			// so it asks here first. Keyed by id rather than name because renames are
			// applied in the same pass that records these.
			std::unordered_map<ECS::Entity, std::set<std::string>> removed_components;
			Core::FlatMap<std::string, Core::MaterialData> materials{ ECS::MAX_ENTITIES };
			Core::FlatMap<std::string, Core::MeshData> meshes{ ECS::MAX_ENTITIES };
			Core::FlatMap<std::string, Core::ShapeData> shapes{ ECS::MAX_ENTITIES };
			Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>> animations{ ECS::MAX_ENTITIES };

			ECS::Coordinator* coordinator = nullptr;
			ECS::Coordinator* templates_coordinator = nullptr;

			std::shared_ptr<Systems::CameraSystem> camera_system;
			std::shared_ptr<Systems::DirectionalLightSystem> dirlight_system;
			std::shared_ptr<Systems::PhysicsSystem> physics_system;
			std::shared_ptr<Systems::PointLightSystem> pointlight_system;
			std::shared_ptr<Systems::RenderSystem> render_system;
			std::shared_ptr<Systems::StaticMeshSystem> static_mesh_system;
			std::shared_ptr<Systems::SkySystem> sky_system;
			std::shared_ptr<Systems::AnimationMeshSystem> animation_mesh_system;
			std::shared_ptr<Systems::ParticleSystem> particle_system;
			std::shared_ptr<Systems::AudioSystem> audio_system;
			bool running = false;
			bool init = false;
			//Init() has run: entities created after this point (editor spawns/clones)
			//must set up their own physics bodies instead of relying on the Init() pass.
			bool scene_init = false;
			std::unordered_map<std::string, std::shared_ptr<ECS::System>> systems_by_name;
			std::list<int> run_timer_ids[Core::DXCore::NTHREADS];
			std::set<std::string> loaded_files;

			void OnLockStepTick(ECS::Event& ev);
			void SetupCoordinator(ECS::Coordinator* c);
			void LoadSky(const nlohmann::json& sky_info);
			std::set<ECS::Entity> LoadFBX(const std::string& file, bool triangulate, bool relative,
							Core::FlatMap<std::string, Core::MaterialData>& materials,
							Core::FlatMap<std::string, Core::MeshData>& meshes,
							Core::FlatMap<std::string, Core::ShapeData>& shapes,
							ECS::Coordinator* c, Core::VertexBuffer<Core::Vertex>* vb, bool use_animation_names = false);
			void LoadInstances(const nlohmann::json& instances_json);
			static void ParsePhysicsJson(const nlohmann::json& physics_json, Components::Physics& physics);

		public:

			World();
			~World();

			virtual bool PreLoad(Core::DXCore* dx);
			virtual bool Release();
			virtual void SetLockStepSync(bool enabled);
			virtual bool GetLockStepSync(void);
			// Pauses stepping of the physics simulation (and the body->Transform
			// write-back) while every other system keeps running. Meant for editors:
			// dynamic bodies must hold still while the scene is authored, or gravity
			// keeps undoing hand-placed transforms. Bodies keep their poses; resuming
			// continues the simulation from wherever they currently are.
			virtual void SetPhysicsPause(bool paused);
			virtual bool GetPhysicsPause(void) const;
			virtual bool Load(const std::string& scene_file, float* progress = nullptr, std::function<void(float)> OnLoadProgress = nullptr, float progress_unit = 1.0f);
			virtual void Init();
			virtual void SetPostProcessPipeline(Core::PostProcess* pipeline);
			// auto_render: when false, skips registering the main-thread render timer
			// (RenderSystem::Update, i.e. Clear/Draw/Present) while still starting the
			// physics/background/audio timers. Lets a host that hasn't loaded any scene
			// content yet (e.g. an editor showing only a project picker UI) drive its own
			// render tick on its own schedule instead, without waiting on Load()/Init().
			virtual void Run(int render_fps, int background_fps = 0, int physics_fps = 0, bool auto_render = true);
			virtual void Stop();
			virtual void LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names = false);
			virtual void LoadMaterialFiles(const nlohmann::json& materials_info, const std::string& path);
			virtual void LoadMaterialsNode(const nlohmann::json& materials_info,
										  const std::string& texture_path);
			virtual void LoadMultiMaterial(const std::string& name, const nlohmann::json& multi_material_info);
			virtual const std::set<ECS::Entity>& GetTemplateEntities(const std::string& template_name);
			virtual bool IsTemplateLoaded(const std::string& template_name);
			// Rebuilds the GPU vertex/BVH buffers from the current CPU-side mesh data.
			// Init() uploads them exactly once, so meshes added by LoadTemplate/LoadFBX
			// calls made after Init() (e.g. an editor importing objects into a running
			// session) otherwise reference GPU data that was never uploaded and render
			// as nothing. Call from the render thread, between frames.
			virtual void RefreshMeshBuffers();
			// Spawns a new, persistable entity (or set of entities, for multi-part templates)
			// cloned from a named template, at the given base transform. Used both by the
			// "instances" section of Load() and by editor tooling that places objects at runtime.
			// `out_parts`, when given, receives every entity created - a multi-part
			// template produces one per FBX node, of which only the first is returned.
			// Callers applying per-instance data (component overrides) need all of them,
			// or the override would silently land on part 0 alone.
			virtual ECS::Entity SpawnInstance(const std::string& name, const std::string& template_name,
							const float3& position, const float4& rotation, const float3& scale,
							const std::string& material_name = "", const nlohmann::json* physics_json = nullptr,
							std::vector<ECS::Entity>* out_parts = nullptr);
			// Creates a new entity named `new_name` as a copy of the existing scene
			// entity `source_name`: Base flags, Transform, Bounds and the (shared)
			// mesh/material data are copied; Physics parameters are copied and a fresh
			// rigid body is created when the source has one. Only mesh entities
			// (Base+Transform+Bounds+Mesh) can be cloned; lights/cameras/sky return
			// INVALID_ENTITY_ID. Used by the "clones" section of Load() and by editor
			// copy/paste at runtime.
			virtual ECS::Entity CloneEntity(const std::string& new_name, const std::string& source_name);

			// Registers T with the ECS and, when T declares the serialization members
			// (see ECS/Serialization.h), with the component registry as well - so a
			// game's own components become level-authorable and editor-visible without
			// touching the engine or any central list. Components that don't declare
			// them are registered with the ECS exactly as before.
			template<typename T>
			void RegisterComponent(ECS::ComponentPolicy policy = ECS::ComponentPolicy::Full)
			{
				coordinator->RegisterComponent<T>();
				if constexpr (ECS::SerializableComponent<T>) {
					ECS::ComponentRegistry::Instance().Register<T>(policy);
				}
			}

			template<typename T>
			std::shared_ptr<T> RegisterSystem()
			{
				std::shared_ptr<T> system = coordinator->RegisterSystem<T>();
				systems_by_name[typeid(T).name()] = system;
				return system;
			}

			ECS::Coordinator* GetCoordinator() { 
				ECS::Coordinator* ret = nullptr;
				if (init) {
					ret = coordinator;
				}
				return ret; 
			}

			ECS::Coordinator* GetTemplatesCoordinator() {
				ECS::Coordinator* ret = nullptr;
				if (init) {
					ret = templates_coordinator;
				}
				return ret;
			}
			
			// Absolute path of the assets folder this world loaded from. Needed by
			// components that resolve file references during deserialization.
			const std::string& GetAssetsPath() const { return path; }

			// Stand-in assets for a Mesh or Material component created from nothing -
			// added in the editor, or a level record carrying an empty block.
			//
			// They exist so that adding a component is the exact inverse of removing
			// one: a component that can be taken away but never put back is a dead end
			// for whoever is authoring the scene. A newly added Mesh is a unit cube and
			// a newly added Material is plain white, so the entity is immediately
			// visible in the viewport and can then be pointed at a real asset.
			//
			// Created on first use and cached, under "__default_*" names that cannot
			// collide with anything an FBX or .mat file brings in. Null only if the
			// render device is not up yet.
			Core::MaterialData* GetDefaultMaterial();
			Core::MeshData* GetDefaultMesh();

			// The names the two above are registered under. A saved scene references
			// them like any other asset, so deserialization has to recognize them and
			// create the asset on demand rather than reporting it missing.
			static constexpr const char* DEFAULT_MATERIAL_NAME = "__default_material";
			static constexpr const char* DEFAULT_MESH_NAME = "__default_mesh";

			// The context handed to component ToJson/FromJson, bound to this world's
			// scene coordinator.
			ECS::SerializeContext MakeSerializeContext();

			// Applies one level record's component blocks to `e`:
			//
			//     "remove":     ["Physics", ...]        - applied first
			//     "components": {"Physics": {...}, ...} - add-or-update, applied second
			//
			// Removals run first so a record can replace a component wholesale, and so
			// that "strip what the template gave me, then add my own" reads in the order
			// it executes. Unknown component names are reported and skipped rather than
			// aborting the load - a level authored against a game that registers more
			// components than the current binary (the Scene Editor opening a Marbles
			// level) must still load everything it does understand.
			//
			// Shared by the "entities", "instances" and "clones" phases of Load, and by
			// editor tooling applying the same blocks at runtime.
			void ApplyComponents(ECS::Entity e, const nlohmann::json& entry);

			// Whether a level record explicitly removed `component` from this entity.
			// Anything that would otherwise add a component by default must consult
			// this, or the removal silently does not stick.
			bool IsComponentRemoved(ECS::Entity e, const std::string& component) const;

			Core::FlatMap<std::string, Core::MaterialData>& GetMaterials();
			Core::FlatMap<std::string, Core::MeshData>& GetMeshes();
			Core::FlatMap<std::string, Core::ShapeData>& GetShapes();
			Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>& GetSkeletons();
			reactphysics3d::PhysicsWorld* GetPhysicsWorld();

			// The collision mesh Init() gave (or would give) this entity's Physics
			// component: its own FBX mesh shape, or its clone source's. Null when the
			// entity has none, which is also the correct answer for dynamic bodies -
			// they always get a primitive capsule/box/sphere instead. Callers that
			// rebuild a collider (Physics::UpdateShape) must pass exactly this.
			Core::ShapeData* GetEntityShape(const std::string& entity_name);

			// Keeps GetEntityShape working across a rename: collision meshes are keyed
			// by the name the entity was loaded under, so an editor that renames an
			// entity must point the new name at the same shape. No-op when the old
			// name has no shape of its own to inherit.
			void AliasEntityShape(const std::string& old_name, const std::string& new_name);

			template<class T>
			std::shared_ptr<T>  GetSystem() {
				std::shared_ptr<T> system;
				auto it = systems_by_name.find(typeid(T).name());
				if (it != systems_by_name.end()) {
					system = std::dynamic_pointer_cast<T>(it->second);
				}
				return system;
			}
		};
	}
}