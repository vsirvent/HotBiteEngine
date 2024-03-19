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

#include <filesystem>
#include "World.h"
#include <Network/LockStepClient.h>
#include <Core/PhysicsCommon.h>
#include <Components/Sky.h>
#include <Network/Commons.h>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Loader;
using namespace HotBite::Engine::Network::LockStep;

World::World() {
	coordinator = new Coordinator();
	templates_coordinator = new Coordinator();
}

World::~World() {
	Release();
	EventListener::Reset();
	systems_by_name.clear();
	delete coordinator;
	delete templates_coordinator;
	if (phys_world != nullptr) {
		physics_common.destroyPhysicsWorld(phys_world);
	}
}

void World::SetupCoordinator(ECS::Coordinator* c) {
	c->Init();
	c->RegisterComponent<Components::Base>();
	c->RegisterComponent<Components::Bounds>();
	c->RegisterComponent<Components::Camera>();
	c->RegisterComponent<Components::AmbientLight>();
	c->RegisterComponent<Components::DirectionalLight>();
	c->RegisterComponent<Components::PointLight>();
	c->RegisterComponent<Components::Mesh>();
	c->RegisterComponent<Components::Material>();
	c->RegisterComponent<Components::Physics>();
	c->RegisterComponent<Components::Transform>();
	c->RegisterComponent<Components::Player>();
	c->RegisterComponent<Components::Lighted>();
	c->RegisterComponent<Components::Particles>();
	c->RegisterComponent<Components::Sky>();
}

bool World::PreLoad(Core::DXCore* dx) {
	
	dx_core = dx;
	auto settings = reactphysics3d::PhysicsWorld::WorldSettings();
	settings.persistentContactDistanceThreshold = 0.1f;
	phys_world = physics_common.createPhysicsWorld(settings);
	vertex_buffer = new VertexBuffer<Core::Vertex>();
	bvh_buffer = new BVHBuffer();

	SetupCoordinator(coordinator);
	SetupCoordinator(templates_coordinator);

	EventListener::Init(coordinator);

	camera_system = RegisterSystem<Systems::CameraSystem>();
	dirlight_system = RegisterSystem<Systems::DirectionalLightSystem>();
	physics_system = RegisterSystem<Systems::PhysicsSystem>();
	pointlight_system = RegisterSystem<Systems::PointLightSystem>();
	render_system = RegisterSystem<Systems::RenderSystem>();
	static_mesh_system = RegisterSystem<Systems::StaticMeshSystem>();
	sky_system = RegisterSystem<Systems::SkySystem>();
	animation_mesh_system = RegisterSystem<Systems::AnimationMeshSystem>();
	particle_system = RegisterSystem<Systems::ParticleSystem>();
	audio_system = RegisterSystem<Systems::AudioSystem>();

	physics_system->Init(phys_world);
	render_system->Init(dx_core, vertex_buffer, bvh_buffer);

	init = true;

	AddEventListener(LSClient::EVENT_ID_SERVER_TICK, std::bind(&World::OnLockStepTick, this, std::placeholders::_1));
	return true;
}

void World::SetLockStepSync(bool enabled) {
	lockstep_sync = enabled;
	lockstep_init = false;
}

bool World::GetLockStepSync(void) {
	return lockstep_sync;
}

void World::OnLockStepTick(ECS::Event& ev) {
	//This callback is called from NETWORK_LOCKSTEP_TICK_THREAD
	if (lockstep_sync) {
		std::shared_ptr<ServerTick> st = ev.GetParam<std::shared_ptr<ServerTick>>(LSClient::EVENT_PARAM_TICK);
		while (current_background_thread_nsec < current_server_nsec || current_physics_thread_nsec < current_server_nsec) { Sleep(1); }
		//previous line guarantees that backgroud and physics threads are blocked here until we update current_server_nsec value,
		// so it's time to process all commands without mutex for those threads (we need to block render thread that is not blocked)
		render_system->mutex.lock();		
		for (auto const& c : st->GetCommands()) {
			lockstep_tick_ev.SetParam<std::shared_ptr<Command>>(Command::EVENT_PARAM_COMMAND, c);
			coordinator->SendEvent(lockstep_tick_ev);
		}
		render_system->mutex.unlock();
		tick_period = st->GetTickPeriod();
		uint64_t a = tick_period % physics_thread_period;
		uint64_t b = tick_period % background_thread_period;
		assert((a <= 1) && (b <= 1) && "Invalid server lockstep tick period");
		current_server_nsec = st->GetFrame()*tick_period;
		if (lockstep_init == false) {
			assert(current_server_nsec == 0 && "Server already in game.");
			lockstep_init = true;
		}
	}
}

Core::FlatMap<std::string, Core::MaterialData>&
World::GetMaterials() {
	return materials;
}

Core::FlatMap<std::string, Core::MeshData>&
World::GetMeshes() {
	return meshes;
}

Core::FlatMap<std::string, Core::ShapeData>&
World::GetShapes() {
	return shapes;
}

Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>&
World::GetSkeletons() {
	return animations;
}

reactphysics3d::PhysicsWorld* 
World::GetPhysicsWorld() {
	return phys_world;
}

bool World::Release() {
	Stop();
	init = false;
	for (auto& m : materials.GetData()) {
		m.Release();
	}
	materials.Clear();
	for (auto& m : meshes.GetData()) {
		m.Release();
	}
	meshes.Clear();
	animations.Clear();
	
	delete vertex_buffer;
	vertex_buffer = nullptr;
	delete bvh_buffer;
	bvh_buffer = nullptr;
	return true;
}

void World::SetPostProcessPipeline(Core::PostProcess* pipeline) {
	render_system->SetPostProcessPipeline(pipeline);
}

void World::LoadFBX(const std::string& file, bool triangulate, bool relative,
	                Core::FlatMap<std::string, Core::MaterialData>& materials,
	                Core::FlatMap<std::string, Core::MeshData>& meshes,
	                Core::FlatMap<std::string, Core::ShapeData>& shapes,
	                ECS::Coordinator* c, Core::VertexBuffer<Core::Vertex>* vb, bool use_animation_names) {
	std::string full_path_file;
	if (!relative || file.find(":") != std::string::npos) {
		full_path_file = file;
	}
	else {
		full_path_file = path + file;
	}
	
	if (!file.empty() && !loaded_files.contains(full_path_file)) {
		loaded_files.insert(full_path_file);

		FBXLoader loader;
		if (!loader.LoadScene(full_path_file, triangulate)) { throw "Load scene failed"; }

		FbxScene* scene = loader.GetScene();
		//Load materials
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.LoadMaterials(materials, n);
		}
		coordinator->SendEvent(this, EVENT_ID_MATERIALS_LOADED);
		//Load meshes
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.LoadMeshes(meshes, n, vb);
		}
		coordinator->SendEvent(this, EVENT_ID_MESHES_LOADED);
		//Load shapes
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.LoadShapes(shapes, n);
		}
		coordinator->SendEvent(this, EVENT_ID_SHAPES_LOADED);

		//Load animations
		loader.LoadSkeletons(file, animations, scene->GetRootNode(), use_animation_names);

		//Load scene entities
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.ProcessEntity(meshes, materials, shapes, c, n);
		}
	}
}

void World::LoadSky(const json& sky_info) {	
	std::string file = sky_info["file"];
	bool triangulate = sky_info["triangulate"];

	LoadFBX(file, triangulate, true, materials, meshes, shapes, coordinator, vertex_buffer);


	ECS::Entity e = coordinator->GetEntityByName(sky_info["name"]);
	assert(e != ECS::INVALID_ENTITY_ID && "Invalid sky name.");

	coordinator->AddComponent<Components::Sky>(e, Components::Sky{});
	Components::Sky& sky = coordinator->GetComponent<Components::Sky>(e);
	if (sky_info.contains("space_name") && !sky_info["space_name"].empty()) {
		ECS::Entity se = templates_coordinator->GetEntityByName(sky_info["space_name"]);
		if (se != ECS::INVALID_ENTITY_ID) {
			sky.space_material = templates_coordinator->GetComponent<Components::Material>(se).data;
			sky.space_mesh = templates_coordinator->GetComponent<Components::Mesh>(se).GetData();
		}
	}
	if (sky_info.contains("day_backcolor")) {
		sky.day_backcolor = ColorRGBFromStr(sky_info["day_backcolor"]);
	}
	if (sky_info.contains("mid_backcolor")) {
		sky.mid_backcolor = ColorRGBFromStr(sky_info["mid_backcolor"]);
	}
	if (sky_info.contains("night_backcolor")) {
		sky.night_backcolor = ColorRGBFromStr(sky_info["night_backcolor"]);
	}
	if (sky_info.contains("second_of_day")) {
		sky.SetTimeOfDay(0, 0, (int)sky_info["second_of_day"]);
	}
	if (sky_info.contains("second_speed")) {
		sky.second_speed = sky_info["second_speed"];
	}
	if (sky_info.contains("cloud_density")) {
		sky.cloud_density = sky_info["cloud_density"];
	}
	if (sky_info.contains("sun")) {
		coordinator->AddComponent<Components::DirectionalLight>(e, Components::DirectionalLight{});
		Components::DirectionalLight& directional = coordinator->GetComponent<Components::DirectionalLight>(e);
		sky.dir_light = &directional;
		const json& light = sky_info["sun"];
		float3 direction = {};
		if (light.contains("direction")) {
			direction = float3{ light["direction"]["x"], light["direction"]["y"], light["direction"]["z"] };
		}
		directional.Init(ColorRGBFromStr(light["color"]), direction, light["cast_shadow"], light["resolution"], light["density"]);
		if (light.contains("skip")) {
			for (const auto& s : light["skip"]) {
				ECS::Entity skip = coordinator->GetEntityByName(s);
				directional.AddSkipEntity(skip);
			}
		}
		if (light.contains("range")) {
			directional.SetRange(light["range"]);
		}
		directional.SetFog(light.value("fog", false));
		directional.SetInverse(light.value("inverse_shadow", false));
	}
	if (sky_info.contains("ambient")) {
		coordinator->AddComponent<Components::AmbientLight>(e, Components::AmbientLight{});
		Components::AmbientLight& ambient = coordinator->GetComponent<Components::AmbientLight>(e);
		const json& light = sky_info["ambient"];
		ambient.GetData().colorUp = ColorRGBFromStr(light["color_up"]);
		ambient.GetData().colorDown = ColorRGBFromStr(light["color_down"]);
	}
	coordinator->NotifySignatureChange(e);	
}

void  World::LoadMaterialFiles(const nlohmann::json& materials_info, const std::string& path) {
	for (const std::string& file: materials_info) {
		std::string file_path = path + file;
		auto input_file = std::ifstream(file_path);
		json scene = json::parse(input_file);

		const std::string root = path + (std::string)scene["root"];
		auto c = GetCoordinator();
		std::scoped_lock l(c->GetSystem<RenderSystem>()->mutex);

		auto& materials = GetMaterials();
		for (const auto& m : scene["materials"]) {
			std::string name = m["name"];
			MaterialData* mdata = materials.Get(name);
			if (mdata == nullptr) {
				materials.Insert(name, MaterialData{ name });
				mdata = materials.Get(name);
			}
			mdata->Load(root, m.dump());
		}
	}
}

void World::LoadMultiMaterial(const std::string& name, const nlohmann::json& multi_material_info) {
	multi_materials[name] = multi_material_info;
}

void World::LoadMaterialsNode(const nlohmann::json& materials_info,
	const std::string& texture_path) {
	//Complete materials information
	for (auto& mat_json : materials_info) {
		std::string name = mat_json["name"];
		std::list<MaterialData*> material_list = materials.GetStrMatch(name);
		if (material_list.empty()) {
			materials.Insert(name, MaterialData{name});
			material_list = materials.GetStrMatch(name);
		}
		//assert(!material_list.empty() && "material not found.");
		for (MaterialData* m : material_list) {
			m->Load(texture_path, mat_json.dump());
		}
	}
}

void World::LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names) {
	LoadFBX(template_file, triangulate, relative, materials, meshes, shapes, templates_coordinator, vertex_buffer, use_animation_names);
}

bool World::Load(const std::string& scene_file, float* progress, std::function<void(float)> OnLoadProgress, float progress_unit) {
	bool ret = true;
	try {
		//Load json world
		json scene = json::parse(std::ifstream(scene_file));
		json& jw = scene["world"];
		path = jw["path"];
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 5.0f * progress_unit); }
		//Load the FBX scene, entities
		//can point to already created materials and meshes
		json& world_fbx = jw["level"];
		{
			std::string file = world_fbx["file"];
			bool triangulate = world_fbx["triangulate"];
			LoadFBX(file, triangulate, true, materials, meshes, shapes, coordinator, vertex_buffer);
			if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 5.0f * progress_unit); }
		}

		//Load templates
		if (jw.contains("templates")) {
			auto& template_files = jw["templates"];
			for (json& t : template_files) {
				LoadTemplate(t["file"], t["triangulate"], true);
			}
			coordinator->SendEvent(this, EVENT_ID_TEMPLATES_LOADED);
			if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }
		}

		//Load sky
		if (jw.contains("sky")) {
			LoadSky(jw["sky"]);
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

		//Complete materials information
		if (jw.contains("materials")) {
			LoadMaterialsNode(jw["materials"], path);
		}

		if (jw.contains("material_files")) {
			LoadMaterialFiles(jw["material_files"], path);
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

		//Complete meshes information
		for (auto& mesh_json : jw["meshes"]) {
			std::list<MeshData*> mesh_list = meshes.GetStrMatch(mesh_json["name"]);
			assert(!mesh_list.empty() && "mesh not found.");
			for (MeshData* m : mesh_list) {
				std::string name = mesh_json["name"];
				if (mesh_json.contains("normal_map_texture")) {
					m->mesh_normal_texture = path + (std::string)mesh_json["normal_map_texture"];
				}
				if (mesh_json.contains("tess_type")) {
					m->tessellation_type = mesh_json["tess_type"];
				}
				if (mesh_json.contains("tess_factor")) {
					m->tessellation_factor = mesh_json["tess_factor"];
				}
				if (mesh_json.contains("displacement_scale")) {
					m->displacement_scale = mesh_json["displacement_scale"];
				}
				
				if (mesh_json.contains("animations")) {
					auto& json_animations = mesh_json["animations"];
					for (std::string animation_name : json_animations) {
						bool done = false;
						std::shared_ptr<Skeleton>* skl = animations.Get(animation_name);
						if (skl != nullptr) {
							m->AddSkeleton(*skl);
						}
						else {
							assert(false && "Animation not found!");
						}
					}
				}
			}
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

		//Complete lights information
		for (auto& light : jw["lights"]) {
			std::string name = light["name"];
			std::string type = light["type"];
			auto light_entities = coordinator->GetEntitiesByName(name);
			if (light_entities.empty()) {
				light_entities.push_back(coordinator->CreateEntity(name));
			}
			for (auto e : light_entities) {
				if (type == "ambient") {
					{
						Components::Base base;
						Components::AmbientLight ambient;
						base.name = name;
						base.id = e;
						ambient.GetData().colorUp = ColorRGBFromStr(light["color_up"]);
						ambient.GetData().colorDown = ColorRGBFromStr(light["color_down"]);

						coordinator->AddComponent<Components::Base>(e, std::move(base));
						coordinator->AddComponent<Components::AmbientLight>(e, std::move(ambient));
						coordinator->NotifySignatureChange(e);
					}
				}
				else if (type == "directional") {
					coordinator->AddComponent<Components::Base>(e, Components::Base{});
					coordinator->AddComponent<Components::DirectionalLight>(e, Components::DirectionalLight{});
					Components::Base& base = coordinator->GetComponent<Components::Base>(e);
					Components::DirectionalLight& directional = coordinator->GetComponent<Components::DirectionalLight>(e);
					base.name = name;
					base.id = e;
					directional.Init(ColorRGBFromStr(light["color"]), float3{ light["direction"]["x"], light["direction"]["y"], light["direction"]["z"] }, light["cast_shadow"], light["resolution"], light["density"]);
					if (light.contains("parent")) {
						ECS::Entity p = coordinator->GetEntityByName(light["parent"]);
						directional.SetParent(p);
						directional.AddSkipEntity(p);
						directional.SetPosition(coordinator->GetComponent<Components::Transform>(p).position);
					}
					if (light.contains("skip")) {
						for (const auto& s : light["skip"]) {
							ECS::Entity skip = coordinator->GetEntityByName(s);
							directional.AddSkipEntity(skip);
						}
					}
					if (light.contains("range")) {
						directional.SetRange(light["range"]);
					}
					directional.SetFog(light.value("fog", false));
					directional.SetInverse(light.value("inverse_shadow", false));
					coordinator->NotifySignatureChange(e);
				}
				else if (type == "point") {
					coordinator->AddComponentIfNotExists<Components::Base>(e, Components::Base{});
					coordinator->AddComponentIfNotExists<Components::Transform>(e, Components::Transform{});
					coordinator->AddComponentIfNotExists<Components::PointLight>(e, Components::PointLight{});
					Components::Base& base = coordinator->GetComponent<Components::Base>(e);
					Components::PointLight& point = coordinator->GetComponent<Components::PointLight>(e);
					Components::Transform& transform = coordinator->GetComponent<Components::Transform>(e);
					if (coordinator->ContainsComponent<Components::Mesh>(e)) {
						coordinator->RemoveComponent<Components::Mesh>(e);
					}
					if (coordinator->ContainsComponent<Components::Material>(e)) {
						coordinator->RemoveComponent<Components::Material>(e);
					}
					base.name = name;
					base.id = e;
					point.Init(ColorRGBFromStr(light["color"]), light["range"], light["cast_shadow"], light["resolution"], light["density"]);
					if (light.contains("position")) {
						transform.position = { light["position"]["x"], light["position"]["y"], light["position"]["z"] };
					}
					coordinator->NotifySignatureChange(e);
				}
			}
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

		//Complete entities information
		for (auto& entity : jw["entities"]) {
			std::string name = entity["name"];
			std::list<ECS::Entity> entity_list = coordinator->GetEntitiesByName(name);
			assert(!entity_list.empty() && "entity not found.");
			for (ECS::Entity e : entity_list) {
				assert(e != ECS::INVALID_ENTITY_ID && "Unknown entity.");
				bool changed = false;
				if (entity.contains("cast_shadow")) {
					Components::Base& base = coordinator->GetComponent<Components::Base>(e);
					base.cast_shadow = entity["cast_shadow"];
					changed = true;
				}					
				if (entity.contains("pass")) {
					Components::Base& base = coordinator->GetComponent<Components::Base>(e);
					base.pass = entity["pass"];
					changed = true;
				}
				if (entity.contains("player")) {
					bool player = entity["player"];
					if (player) {
						coordinator->AddComponent<Components::Player>(e, Components::Player{});
						changed = true;
					}
				}
				if (entity.contains("parent")) {
					std::string parent_name = entity["parent"];
					if (!parent_name.empty()) {
						ECS::Entity pe = coordinator->GetEntityByName(parent_name);
						assert(pe != ECS::INVALID_ENTITY_ID && "Unknown parent.");
						Components::Base& base = coordinator->GetComponent<Components::Base>(e);
						base.parent = pe;
						changed = true;
					}
				}
				if (entity.contains("position")) {
					json& pos = entity["position"];
					Components::Transform& t = coordinator->GetComponent<Components::Transform>(e);
					t.position.x = pos["x"];
					t.position.y = pos["y"];
					t.position.z = pos["z"];
					t.dirty = true;
					changed = true;
				}
				if (entity.contains("scale")) {
					json& scl = entity["scale"];
					Components::Transform& t = coordinator->GetComponent<Components::Transform>(e);
					t.scale.x = scl["x"];
					t.scale.y = scl["y"];
					t.scale.z = scl["z"];
					t.dirty = true;
					changed = true;
				}
				if (entity.contains("rotation")) {
					json& rot = entity["rotation"];
					Components::Transform& t = coordinator->GetComponent<Components::Transform>(e);
					t.rotation.x = rot["x"];
					t.rotation.y = rot["y"];
					t.rotation.z = rot["z"];
					t.rotation.w = rot["w"];
					t.dirty = true;
					changed = true;
				}
				if (entity.contains("physics")) {
					auto& physics_json = entity["physics"];
					Components::Physics physics;
					physics.type = reactphysics3d::BodyType::STATIC;
					if (physics_json.contains("type")) {
						const std::string& type = physics_json["type"];
						if (type == "DYNAMIC") {
							physics.type = reactphysics3d::BodyType::DYNAMIC;
						}
						else if (type == "KINEMATIC") {
							physics.type = reactphysics3d::BodyType::KINEMATIC;
						}
						else {
							assert(false && "Unknown physics type.");
						}
					}
					if (physics_json.contains("bounce")) {
						physics.bounce = physics_json["bounce"];
					}
					if (physics_json.contains("friction")) {
						physics.friction = physics_json["friction"];
					}
					if (physics_json.contains("air_friction")) {
						physics.air_friction = physics_json["air_friction"];
					}
					if (physics_json.contains("shape")) {
						const std::string& shape = physics_json["shape"];
						if (shape == "CAPSULE") {
							physics.shape = Physics::SHAPE_CAPSULE;
						}
						else if (shape == "SPHERE") {
							physics.shape = Physics::SHAPE_SPHERE;
						}
						else if (shape == "BOX") {
							physics.shape = Physics::SHAPE_BOX;
						}
						else {
							assert(false && "Unknown physics shape.");
						}
					}
					coordinator->AddComponent<Components::Physics>(e, std::move(physics));
					changed = true;
				}
				if (entity.contains("template")) {
					std::string template_entity_name = entity["template"];
					if (!template_entity_name.empty()) {
						ECS::Entity te = templates_coordinator->GetEntityByName(template_entity_name);
						if (te != ECS::INVALID_ENTITY_ID) {
							Components::Mesh& tmesh = templates_coordinator->GetComponent<Components::Mesh>(te);
							Components::Material& tmat = templates_coordinator->GetComponent<Components::Material>(te);

							Components::Mesh& mesh = coordinator->GetComponent<Components::Mesh>(e);
							Components::Material& mat = coordinator->GetComponent<Components::Material>(e);

							mesh.SetData(tmesh.GetData());
							mat.data = tmat.data;
							changed = true;
						}
					}
				}
				if (entity.contains("material")) {
					auto& material = entity["material"];
					Components::Material& m = coordinator->GetComponent<Components::Material>(e);
					auto mat = GetMaterials().Get(material);
					if (mat != nullptr) {
						m.data = mat;
					}
				}
				if (entity.contains("multi_texture")) {
					auto& multi_textures_json = entity["multi_texture"];
					Components::Material &m = coordinator->GetComponent<Components::Material>(e);
					std::string name = multi_textures_json["name"];
					const auto mt = multi_materials.find(name);
					if (mt != multi_materials.end()) {
						m.multi_material.LoadMultitexture(mt->second, path, materials);
					}
					else {
						m.multi_material.LoadMultitexture(multi_textures_json.dump(), path, materials);
					}
				}
				if (changed) {
					coordinator->NotifySignatureChange(e);
				}
			}
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

		if (jw.contains("audio")) {
			//Audio config load
			audio_system->Config(path, jw["audio"]);
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }

	}
	catch (std::exception& e) {
		printf("World::Load: Fail: %s\n", e.what());
		assert(false && "Bad world.");
	}

	printf("Worl load DONE: %llu entities loaded\n", coordinator->GetEntites().size());
	return ret;
}

void World::Init() {
	//End initialization
	for (auto& m : materials.GetData()) {
		//Init material
		m.Init();
	}
	//Init multitextures
	for (auto& m : coordinator->GetComponents<Components::Material>()->Array()) {
		for (uint32_t i = 0; i < m.multi_material.multi_texture_count; ++i) {
			if (m.multi_material.multi_texture_data[i] != nullptr) {
				if (m.multi_material.multi_texture_data[i]->diffuse != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_DIFF;
				}
				if (m.multi_material.multi_texture_data[i]->normal != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_NORM;
				}
				if (m.multi_material.multi_texture_data[i]->spec != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_SPEC;
				}
				if (m.multi_material.multi_texture_data[i]->ao != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_AO;
				}
				if (m.multi_material.multi_texture_data[i]->arm != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_ARM;
				}
				if (m.multi_material.multi_texture_data[i]->high != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_DISP;
				}
				if (m.multi_material.multi_texture_mask[i] != nullptr) {
					m.multi_material.multi_texture_operation[i] |= TEXT_MASK;
				}
			}
		}
	}
	for (auto& m : meshes.GetData()) {
		//Normal map textures are loaded from json file,
		//so we need to load in a second stage (after mesh.Init())
		m.LoadTextures();
	}

	//Init physics
	for (auto& e : coordinator->GetEntites()) {
		Components::Base& base = coordinator->GetComponent<Components::Base>(e.second);
		if (coordinator->ContainsComponent<Components::Mesh>(e.second)) {
			printf("Entity %s\n", base.name.c_str());
			ShapeData* shape = nullptr;
			if (coordinator->ContainsComponent<Components::Sky>(e.second)) {
				continue;
			}
			if (!coordinator->ContainsComponent<Components::Physics>(e.second)) {
				coordinator->AddComponent(e.second, Components::Physics{});
				coordinator->NotifySignatureChange(e.second);
			}
			Components::Physics& p = coordinator->GetComponent<Components::Physics>(e.second);
			Components::Transform& t = coordinator->GetComponent<Components::Transform>(e.second);
			Components::Bounds& b = coordinator->GetComponent<Components::Bounds>(e.second);
			Components::Base& base = coordinator->GetComponent<Components::Base>(e.second);
			if (p.type != reactphysics3d::BodyType::DYNAMIC) {
				//Dynamic bodies use capsules, can't use mesh shape
				shape = shapes.Get(e.first);
			}
			if (shape == nullptr) {
				printf("No shape for mesh %s\n", e.first.c_str());
			}
			p.Init(phys_world, p.type, shape, b.bounding_box.Extents, t.position, t.scale, t.rotation, p.shape);
			
		}
	}	
	vertex_buffer->Prepare();
	for (auto& m : meshes.GetData()) {
		size_t offset = 0;
		bvh_buffer->Add(m.bvh.Root(), m.bvh.Size(), &m.bvhOffset);
	}
	bvh_buffer->Prepare();
}

void World::Run(int render_fps, int background_fps, int physics_fps) {
	if (!running) {
		running = true;
		if (background_fps != 0) {
			background_thread_period = 1000000000 / background_fps;
		}
		if (physics_fps != 0) {
			physics_thread_period = 1000000000 / physics_fps;
		}
		coordinator->GetSystem<AudioSystem>()->Start();

		if (run_timer_ids[DXCore::MAIN_THREAD].empty()) {

			physics_system->Update(0, 0, true);

			run_timer_ids[DXCore::MAIN_THREAD].push_back(Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / render_fps, [this](const Scheduler::TimerData& t) {
				//Update render system that don't need sync with lockstep
				particle_system->Update(t.period, t.total);
				render_system->Update();
				render_system->mutex.lock();
				coordinator->SendEvent(this, World::EVENT_ID_UPDATE_MAIN);
				render_system->mutex.unlock();
				return true;
				}));

			run_timer_ids[DXCore::BACKGROUND_THREAD].push_back(Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(background_thread_period, [this](const Scheduler::TimerData& t) {
				//Update systems that need sync with lockstep
				if (lockstep_sync) {
					while (current_background_thread_nsec >= current_server_nsec) { Sleep(1); }
				}
				render_system->mutex.lock();
				physics_mutex.lock();
				//physics systems moves information to transform component used by the renderer,
				//so we need to take the renderer lock for this
				phys_world->update((float)t.period / 1000000000.0f);
				physics_system->Update(t.period, t.total, false);
				camera_system->Update(t.period, t.total);
				coordinator->SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND);
				physics_mutex.unlock();
				render_system->mutex.unlock();
				current_background_thread_nsec += background_thread_period;
				return true;
				}));

			run_timer_ids[DXCore::BACKGROUND2_THREAD].push_back(Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RegisterTimer(background_thread_period, [this](const Scheduler::TimerData& t) {
				//Update systems that don't need sync with lockstep nor physics dependencies
				render_system->mutex.lock();
				sky_system->Update(t.period, t.total);
				animation_mesh_system->Update(t.period, t.total);
				coordinator->SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND2);
				render_system->mutex.unlock();
				return true;
				}));

			run_timer_ids[DXCore::BACKGROUND3_THREAD].push_back(Scheduler::Get(DXCore::BACKGROUND3_THREAD)->RegisterTimer(background_thread_period, [this](const Scheduler::TimerData& t) {
				//Update systems that don't need sync with lockstep but physics dependencies,
				//Entities can have parents whose transform is updated by physics so we need 
				//to take the physics lock for this
				render_system->mutex.lock();
				physics_mutex.lock();
				static_mesh_system->Update(t.period, t.total);
				dirlight_system->Update(t.period, t.total);
				pointlight_system->Update(t.period, t.total);
				coordinator->SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND3);
				physics_mutex.unlock();
				render_system->mutex.unlock();
				return true;
				}));

			run_timer_ids[DXCore::PHYSICS_THREAD].push_back(Scheduler::Get(DXCore::PHYSICS_THREAD)->RegisterTimer(physics_thread_period, [this](const Scheduler::TimerData& t) {
				//Update physics that need sync with lockstep
				if (lockstep_sync) {
					while (current_physics_thread_nsec >= current_server_nsec) { Sleep(1); }
				}
				physics_mutex.lock();
				//phys_world->update((float)t.period / 1000000000.0f);			
				coordinator->SendEvent(this, World::EVENT_ID_UPDATE_PHYSICS);
				physics_mutex.unlock();
				current_physics_thread_nsec += physics_thread_period;
				return true;
				}));
		}
	}
}

void World::Stop() {
	if (running) {
		running = false;
		for (int i = 0; i < DXCore::NTHREADS; ++i) {
			render_system->mutex.lock();
			physics_mutex.lock();
			while (!run_timer_ids[i].empty()) {
				Scheduler::Get(i)->RemoveTimerAsync(run_timer_ids[i].front());
				run_timer_ids[i].pop_front();
			}
			coordinator->GetSystem<AudioSystem>()->Stop();
			physics_mutex.unlock();
			render_system->mutex.unlock();
		}
	}
}

