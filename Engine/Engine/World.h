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
			ECS::Event lockstep_tick_ev{ this, Network::LockStep::Command::EVENT_ID_NEW_SERVER_COMMAND };
			
			Core::VertexBuffer<Core::Vertex>* vertex_buffer = nullptr;
			BVHBuffer* bvh_buffer = nullptr;
			reactphysics3d::PhysicsWorld* phys_world = nullptr;
			Core::DXCore* dx_core;

			//Absolute path of assets location
			std::string path;
			std::unordered_map<std::string, nlohmann::json> multi_materials;
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
			std::unordered_map<std::string, std::shared_ptr<ECS::System>> systems_by_name;
			std::list<int> run_timer_ids[Core::DXCore::NTHREADS];
			std::set<std::string> loaded_files;

			void OnLockStepTick(ECS::Event& ev);
			void SetupCoordinator(ECS::Coordinator* c);
			void LoadSky(const nlohmann::json& sky_info);
			void LoadFBX(const std::string& file, bool triangulate, bool relative,
							Core::FlatMap<std::string, Core::MaterialData>& materials,
							Core::FlatMap<std::string, Core::MeshData>& meshes,
							Core::FlatMap<std::string, Core::ShapeData>& shapes,
							ECS::Coordinator* c, Core::VertexBuffer<Core::Vertex>* vb, bool use_animation_names = false);
			
		public:			

			World();
			~World();

			virtual bool PreLoad(Core::DXCore* dx);
			virtual bool Release();
			virtual void SetLockStepSync(bool enabled);
			virtual bool GetLockStepSync(void);
			virtual bool Load(const std::string& scene_file, std::function<void(float)> OnLoadProgress = nullptr);
			virtual void Init();
			virtual void SetPostProcessPipeline(Core::PostProcess* pipeline);
			virtual void Run(int render_fps, int background_fps = 0, int physics_fps = 0);
			virtual void Stop();
			virtual void LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names = false);
			virtual void LoadMaterialFiles(const nlohmann::json& materials_info, const std::string& path);
			virtual void LoadMaterialsNode(const nlohmann::json& materials_info,
										  const std::string& texture_path);
			virtual void LoadMultiMaterial(const std::string& name, const nlohmann::json& multi_material_info);

			template<typename T>
			void RegisterComponent()
			{
				coordinator->RegisterComponent<T>();
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
			
			Core::FlatMap<std::string, Core::MaterialData>& GetMaterials();
			Core::FlatMap<std::string, Core::MeshData>& GetMeshes();
			Core::FlatMap<std::string, Core::ShapeData>& GetShapes();
			Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>& GetSkeletons();
			reactphysics3d::PhysicsWorld* GetPhysicsWorld();

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