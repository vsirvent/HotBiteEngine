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
}

World::~World() {
	Release();
	EventListener::Reset();
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
	phys_world = physics_common.createPhysicsWorld();
	vertex_buffer = new VertexBuffer<Core::Vertex>();

	SetupCoordinator(&coordinator);
	SetupCoordinator(&templates_coordinator);

	EventListener::Init(&coordinator);

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
	render_system->Init(dx_core, vertex_buffer);

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
			coordinator.SendEvent(lockstep_tick_ev);
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
	if (phys_world != nullptr) {
		physics_common.destroyPhysicsWorld(phys_world);
		phys_world = nullptr;
	}
	delete vertex_buffer;
	vertex_buffer = nullptr;
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
		coordinator.SendEvent(this, EVENT_ID_MATERIALS_LOADED);
		//Load meshes
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.LoadMeshes(meshes, n, vb);
		}
		coordinator.SendEvent(this, EVENT_ID_MESHES_LOADED);
		//Load shapes
		for (int i = 0; i < scene->GetRootNode()->GetChildCount(); ++i) {
			fbxsdk::FbxNode* n = scene->GetRootNode()->GetChild(i);
			loader.LoadShapes(shapes, n);
		}
		coordinator.SendEvent(this, EVENT_ID_SHAPES_LOADED);

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

	LoadFBX(file, triangulate, true, materials, meshes, shapes, &coordinator, vertex_buffer);
	ECS::Entity e = coordinator.GetEntityByName(sky_info["name"]);
	assert(e != ECS::INVALID_ENTITY_ID && "Invalid sky name.");

	coordinator.AddComponent<Components::Sky>(e, Components::Sky{});
	Components::Sky& sky = coordinator.GetComponent<Components::Sky>(e);
	if (sky_info.contains("space_name") && !sky_info["space_name"].empty()) {
		ECS::Entity se = templates_coordinator.GetEntityByName(sky_info["space_name"]);
		if (se != ECS::INVALID_ENTITY_ID) {
			sky.space_material = templates_coordinator.GetComponent<Components::Material>(se).data;
			sky.space_mesh = templates_coordinator.GetComponent<Components::Mesh>(se).GetData();
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
		coordinator.AddComponent<Components::DirectionalLight>(e, Components::DirectionalLight{});
		Components::DirectionalLight& directional = coordinator.GetComponent<Components::DirectionalLight>(e);
		sky.dir_light = &directional;
		const json& light = sky_info["sun"];
		float3 direction = {};
		if (light.contains("direction")) {
			direction = float3{ light["direction"]["x"], light["direction"]["y"], light["direction"]["z"] };
		}
		directional.Init(ColorRGBFromStr(light["color"]), direction, light["cast_shadow"], light["resolution"], light["density"]);
		if (light.contains("skip")) {
			for (const auto& s : light["skip"]) {
				ECS::Entity skip = coordinator.GetEntityByName(s);
				directional.AddSkipEntity(skip);
			}
		}
		if (light.contains("range")) {
			directional.SetRange(light["range"]);
		}
		if (light.contains("fog")) {
			directional.SetFog(light["fog"]);
		}
	}
	if (sky_info.contains("ambient")) {
		coordinator.AddComponent<Components::AmbientLight>(e, Components::AmbientLight{});
		Components::AmbientLight& ambient = coordinator.GetComponent<Components::AmbientLight>(e);
		const json& light = sky_info["ambient"];
		ambient.GetData().colorUp = ColorRGBFromStr(light["color_up"]);
		ambient.GetData().colorDown = ColorRGBFromStr(light["color_down"]);
	}
	coordinator.NotifySignatureChange(e);	
}

void  World::LoadMaterialsNode(const nlohmann::json& materials_info,
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
			if (mat_json.contains("diffuse_color")) {
				m->props.diffuseColor = ColorFromStr(mat_json["diffuse_color"]);
			}
			if (mat_json.contains("ambient_color")) {
				m->props.ambientColor = ColorFromStr(mat_json["ambient_color"]);
			}
			if (mat_json.contains("spec_intensity")) {
				m->props.specIntensity = mat_json["spec_intensity"];
			}
			if (mat_json.contains("parallax_scale")) {
				m->props.parallax_scale = mat_json["parallax_scale"];
			}
			if (mat_json.contains("tess_type")) {
				m->tessellation_type = mat_json["tess_type"];
			}
			if (mat_json.contains("tess_factor")) {
				m->tessellation_factor = mat_json["tess_factor"];
			}
			if (mat_json.contains("displacement_scale")) {
				m->displacement_scale = mat_json["displacement_scale"];
			}
			if (mat_json.contains("bloom_scale")) {
				m->props.bloom_scale = mat_json["bloom_scale"];
			}
			if (mat_json.contains("normal_map_enabled")) {
				if (mat_json["normal_map_enabled"]) {
					m->props.flags |= NORMAL_MAP_ENABLED_FLAG;
				}
				else {
					m->props.flags &= ~NORMAL_MAP_ENABLED_FLAG;
				}
			}
			if (mat_json.contains("alpha_enabled")) {
				if (mat_json["alpha_enabled"]) {
					m->props.flags |= ALPHA_ENABLED_FLAG;
					if (mat_json.contains("alpha_color")) {
						m->props.alphaColor = ColorRGBFromStr(mat_json["alpha_color"]);
					}
				}
				else {
					m->props.flags &= ~ALPHA_ENABLED_FLAG;
				}
			}
			if (mat_json.contains("blend_enabled")) {
				if (mat_json["blend_enabled"]) {
					m->props.flags |= BLEND_ENABLED_FLAG;
				}
				else {
					m->props.flags &= ~BLEND_ENABLED_FLAG;
				}
			}
			if (mat_json.contains("textures")) {
				auto& textures = mat_json["textures"];
				if (textures.contains("diffuse_textname")) {
					m->texture_names.diffuse_texname = texture_path + (std::string)textures["diffuse_textname"];
				}
				if (textures.contains("normal_textname")) {
					m->texture_names.normal_textname = texture_path + (std::string)textures["normal_textname"];
				}
				if (textures.contains("high_textname")) {
					m->texture_names.high_textname = texture_path + (std::string)textures["high_textname"];
				}
				if (textures.contains("spec_textname")) {
					m->texture_names.spec_textname = texture_path + (std::string)textures["spec_textname"];
				}
				if (textures.contains("ao_textname")) {
					m->texture_names.ao_textname = texture_path + (std::string)textures["ao_textname"];
				}
				if (textures.contains("arm_textname")) {
					m->texture_names.arm_textname = texture_path + (std::string)textures["arm_textname"];
				}
				if (textures.contains("emission_textname")) {
					m->texture_names.emission_textname = texture_path + (std::string)textures["emission_textname"];
				}
			}
			if (mat_json.contains("shaders")) {
				std::string name = mat_json["name"];
				const json& shaders = mat_json["shaders"];
				if (shaders.contains("vs"))
					m->shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>(shaders["vs"]);
				if (shaders.contains("hs"))
					m->shaders.hs = ShaderFactory::Get()->GetShader<SimpleHullShader>(shaders["hs"]);
				if (shaders.contains("ds"))
					m->shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>(shaders["ds"]);
				if (shaders.contains("gs"))
					m->shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>(shaders["gs"]);
				if (shaders.contains("ps"))
					m->shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>(shaders["ps"]);
			}
		}
	}
}

void World::LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names) {
	LoadFBX(template_file, triangulate, relative, materials, meshes, shapes, &templates_coordinator, vertex_buffer, use_animation_names);
}

bool World::Load(const std::string& scene_file) {
	bool ret = true;
	try {
		//Load json world
		json scene = json::parse(std::ifstream(scene_file));
		json& jw = scene["world"];
		path = jw["path"];

		//Load the FBX scene, entities
		//can point to already created materials and meshes
		json& world_fbx = jw["level"];
		{
			std::string file = world_fbx["file"];
			bool triangulate = world_fbx["triangulate"];
			LoadFBX(file, triangulate, true, materials, meshes, shapes, &coordinator, vertex_buffer);
		}

		//Load templates
		if (jw.contains("templates")) {
			auto& template_files = jw["templates"];
			for (json& t : template_files) {
				LoadTemplate(t["file"], t["triangulate"], true);
			}
			coordinator.SendEvent(this, EVENT_ID_TEMPLATES_LOADED);
		}

		//Load sky
		if (jw.contains("sky")) {
			LoadSky(jw["sky"]);
		}

		//Complete materials information
		if (jw.contains("materials")) {
			LoadMaterialsNode(jw["materials"], path);
		}
				
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

		//Complete lights information
		for (auto& light : jw["lights"]) {
			std::string name = light["name"];
			std::string type = light["type"];
			if (type == "ambient") {
				ECS::Entity e = coordinator.GetEntityByName(name);
				if (e == ECS::INVALID_ENTITY_ID) {
					e = coordinator.CreateEntity(name);
				}
				{
					Components::Base base;
					Components::AmbientLight ambient;
					base.name = name;
					base.id = e;
					ambient.GetData().colorUp = ColorRGBFromStr(light["color_up"]);
					ambient.GetData().colorDown = ColorRGBFromStr(light["color_down"]);

					coordinator.AddComponent<Components::Base>(e, std::move(base));
					coordinator.AddComponent<Components::AmbientLight>(e, std::move(ambient));
					coordinator.NotifySignatureChange(e);
				}
			}
			else if (type == "directional") {
				ECS::Entity e = coordinator.GetEntityByName(name);
				if (e == ECS::INVALID_ENTITY_ID) {
					e = coordinator.CreateEntity(name);
				}
				coordinator.AddComponent<Components::Base>(e, Components::Base{});
				coordinator.AddComponent<Components::DirectionalLight>(e, Components::DirectionalLight{});
				Components::Base& base = coordinator.GetComponent<Components::Base>(e);
				Components::DirectionalLight& directional = coordinator.GetComponent<Components::DirectionalLight>(e);
				base.name = name;
				base.id = e;
				directional.Init(ColorRGBFromStr(light["color"]), float3{ light["direction"]["x"], light["direction"]["y"], light["direction"]["z"] }, light["cast_shadow"], light["resolution"], light["density"]);
				if (light.contains("parent")) {
					ECS::Entity p = coordinator.GetEntityByName(light["parent"]);
					directional.SetParent(p);
					directional.AddSkipEntity(p);
					directional.SetPosition(coordinator.GetComponent<Components::Transform>(p).position);
				}
				if (light.contains("skip")) {
					for (const auto& s : light["skip"]) {
						ECS::Entity skip = coordinator.GetEntityByName(s);
						directional.AddSkipEntity(skip);
					}
				}
				if (light.contains("range")) {
					directional.SetRange(light["range"]);
				}
				if (light.contains("fog")) {
					directional.SetFog(light["fog"]);
				}
				coordinator.NotifySignatureChange(e);
			}
			else if (type == "point") {
				ECS::Entity e = coordinator.GetEntityByName(name);
				if (e == ECS::INVALID_ENTITY_ID) {
					e = coordinator.CreateEntity(name);
				}
				coordinator.AddComponent<Components::Base>(e, Components::Base{});
				coordinator.AddComponent<Components::Transform>(e, Components::Transform{});
				coordinator.AddComponent<Components::PointLight>(e, Components::PointLight{});
				Components::Base& base = coordinator.GetComponent<Components::Base>(e);
				Components::PointLight& point = coordinator.GetComponent<Components::PointLight>(e);
				Components::Transform& transform = coordinator.GetComponent<Components::Transform>(e);
				base.name = name;
				base.id = e;
				point.Init(ColorRGBFromStr(light["color"]), light["range"], light["cast_shadow"], light["resolution"], light["density"]);
				transform.position = { light["position"]["x"], light["position"]["y"], light["position"]["z"] };
				coordinator.NotifySignatureChange(e);
			}
		}

		//Complete entities information
		for (auto& entity : jw["entities"]) {
			std::string name = entity["name"];
			std::list<ECS::Entity> entity_list = coordinator.GetEntitiesByName(name);
			assert(!entity_list.empty() && "entity not found.");
			for (ECS::Entity e : entity_list) {
				assert(e != ECS::INVALID_ENTITY_ID && "Unknown entity.");
				bool changed = false;
				if (entity.contains("cast_shadow")) {
					Components::Base& base = coordinator.GetComponent<Components::Base>(e);
					base.cast_shadow = entity["cast_shadow"];
					changed = true;
				}					
				if (entity.contains("pass")) {
					Components::Base& base = coordinator.GetComponent<Components::Base>(e);
					base.pass = entity["pass"];
					changed = true;
				}
				if (entity.contains("player")) {
					bool player = entity["player"];
					if (player) {
						coordinator.AddComponent<Components::Player>(e, Components::Player{});
						changed = true;
					}
				}
				if (entity.contains("parent")) {
					std::string parent_name = entity["parent"];
					if (!parent_name.empty()) {
						ECS::Entity pe = coordinator.GetEntityByName(parent_name);
						assert(pe != ECS::INVALID_ENTITY_ID && "Unknown parent.");
						Components::Base& base = coordinator.GetComponent<Components::Base>(e);
						base.parent = pe;
						changed = true;
					}
				}
				if (entity.contains("position")) {
					json& pos = entity["position"];
					Components::Transform& t = coordinator.GetComponent<Components::Transform>(e);
					t.position.x = pos["x"];
					t.position.y = pos["y"];
					t.position.z = pos["z"];
					t.dirty = true;
					changed = true;
				}
				if (entity.contains("scale")) {
					json& scl = entity["scale"];
					Components::Transform& t = coordinator.GetComponent<Components::Transform>(e);
					t.scale.x = scl["x"];
					t.scale.y = scl["y"];
					t.scale.z = scl["z"];
					t.dirty = true;
					changed = true;
				}
				if (entity.contains("rotation")) {
					json& rot = entity["rotation"];
					Components::Transform& t = coordinator.GetComponent<Components::Transform>(e);
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
					coordinator.AddComponent<Components::Physics>(e, std::move(physics));
					changed = true;
				}
				if (entity.contains("template")) {
					std::string template_entity_name = entity["template"];
					if (!template_entity_name.empty()) {
						ECS::Entity te = templates_coordinator.GetEntityByName(template_entity_name);
						if (te != ECS::INVALID_ENTITY_ID) {
							Components::Mesh& tmesh = templates_coordinator.GetComponent<Components::Mesh>(te);
							Components::Material& tmat = templates_coordinator.GetComponent<Components::Material>(te);

							Components::Mesh& mesh = coordinator.GetComponent<Components::Mesh>(e);
							Components::Material& mat = coordinator.GetComponent<Components::Material>(e);

							mesh.SetData(tmesh.GetData());
							mat.data = tmat.data;
							changed = true;
						}
					}
				}
				if (entity.contains("multi_texture")) {
					auto& multi_textures_json = entity["multi_texture"];
					Components::Material &m = coordinator.GetComponent<Components::Material>(e);
					m.multi_texture_count = multi_textures_json["count"];
					m.multi_texture_data.resize(m.multi_texture_count);
					m.multi_texture_mask.resize(m.multi_texture_count);
					m.multi_texture_operation.resize(m.multi_texture_count);
					m.multi_texture_uv_scales.resize(m.multi_texture_count);
					m.multi_texture_value.resize(m.multi_texture_count);
					if (multi_textures_json.contains("parallax_scale")) {
						m.multi_parallax_scale = multi_textures_json["parallax_scale"];
					}
					if (multi_textures_json.contains("tess_type")) {
						m.tessellation_type = multi_textures_json["tess_type"];
					}
					if (multi_textures_json.contains("tess_factor")) {
						m.tessellation_factor = multi_textures_json["tess_factor"];
					}
					if (multi_textures_json.contains("displacement_scale")) {
						m.displacement_scale = multi_textures_json["displacement_scale"];
					}
					for (auto& multi_texture : multi_textures_json["textures"]) {
						int layer = multi_texture["layer"];
						m.multi_texture_mask[layer] = LoadTexture(path + (std::string)multi_texture["mask"]);
						m.multi_texture_operation[layer] = multi_texture["op"];
						m.multi_texture_uv_scales[layer] = multi_texture["uv_scale"];
						m.multi_texture_value[layer] = multi_texture["value"];
						m.multi_texture_data[layer] = materials.Get(multi_texture["texture"]);
						if (multi_texture.contains("mask_noise") && multi_texture["mask_noise"] == 1) {
							m.multi_texture_operation[layer] |= TEXT_MASK_NOISE;
						}
						if (multi_texture.contains("uv_noise") && multi_texture["uv_noise"] == 1) {
							m.multi_texture_operation[layer] |= TEXT_UV_NOISE;
						}
					}
				}
				if (changed) {
					coordinator.NotifySignatureChange(e);
				}
			}
		}

		//Audio config load
		audio_system->Config(path, jw["audio"]);
	}
	catch (std::exception& e) {
		printf("World::Load: Fail: %s\n", e.what());
		assert(false && "Bad world.");
	}

	printf("Worl load DONE: %llu entities loaded\n", coordinator.GetEntites().size());
	return ret;
}

void World::Init() {
	//End initialization
	for (auto& m : materials.GetData()) {
		//Init material
		m.Init();
	}
	//Init multitextures
	for (auto& m : coordinator.GetComponents<Components::Material>()->Array()) {
		for (uint32_t i = 0; i < m.multi_texture_count; ++i) {
			if (m.multi_texture_data[i] != nullptr) {
				if (m.multi_texture_data[i]->diffuse != nullptr) {
					m.multi_texture_operation[i] |= TEXT_DIFF;
				}
				if (m.multi_texture_data[i]->normal != nullptr) {
					m.multi_texture_operation[i] |= TEXT_NORM;
				}
				if (m.multi_texture_data[i]->spec != nullptr) {
					m.multi_texture_operation[i] |= TEXT_SPEC;
				}
				if (m.multi_texture_data[i]->ao != nullptr) {
					m.multi_texture_operation[i] |= TEXT_AO;
				}
				if (m.multi_texture_data[i]->arm != nullptr) {
					m.multi_texture_operation[i] |= TEXT_ARM;
				}
				if (m.multi_texture_data[i]->high != nullptr) {
					m.multi_texture_operation[i] |= TEXT_DISP;
				}
				if (m.multi_texture_mask[i] != nullptr) {
					m.multi_texture_operation[i] |= TEXT_MASK;
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
	for (auto& e : coordinator.GetEntites()) {
		Components::Base& base = coordinator.GetComponent<Components::Base>(e.second);
		if (coordinator.ContainsComponent<Components::Mesh>(e.second)) {
			printf("Entity %s\n", base.name.c_str());
			ShapeData* shape = nullptr;
			if (coordinator.ContainsComponent<Components::Sky>(e.second)) {
				continue;
			}
			if (!coordinator.ContainsComponent<Components::Physics>(e.second)) {
				coordinator.AddComponent(e.second, Components::Physics{});
				coordinator.NotifySignatureChange(e.second);
			}
			Components::Physics& p = coordinator.GetComponent<Components::Physics>(e.second);
			Components::Transform& t = coordinator.GetComponent<Components::Transform>(e.second);
			Components::Bounds& b = coordinator.GetComponent<Components::Bounds>(e.second);
			Components::Base& base = coordinator.GetComponent<Components::Base>(e.second);
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
}

void World::Run(int render_fps, int background_fps, int physics_fps) {
	if (background_fps != 0) {
		background_thread_period = 1000000000 / background_fps;
	}
	if (physics_fps != 0) {
		physics_thread_period = 1000000000 / physics_fps;
	}
	if (run_timer_ids[DXCore::MAIN_THREAD].empty()) {
		
		physics_system->Update(0, 0, true);

		run_timer_ids[DXCore::MAIN_THREAD].push_back(Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / render_fps, [this](const Scheduler::TimerData& t) {
			//Update render system that don't need sync with lockstep
			particle_system->Update(t.period, t.total);
			render_system->Update();
			render_system->mutex.lock();
			coordinator.SendEvent(this, World::EVENT_ID_UPDATE_MAIN);
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
			physics_system->Update(t.period, t.total, false);
			camera_system->Update(t.period, t.total);
			coordinator.SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND);
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
			coordinator.SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND2);
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
			coordinator.SendEvent(this, World::EVENT_ID_UPDATE_BACKGROUND3);
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
			phys_world->update((float)t.period / 1000000000.0f);			
			coordinator.SendEvent(this, World::EVENT_ID_UPDATE_PHYSICS);
			physics_mutex.unlock();
			current_physics_thread_nsec += physics_thread_period;			
			return true;
		}));
	}
}

void World::Stop() {
	for (int i = 0; i < DXCore::NTHREADS; ++i) {
		render_system->mutex.lock();
		physics_mutex.lock();
		while (!run_timer_ids[i].empty()) {
			Scheduler::Get(i)->RemoveTimer(run_timer_ids[i].front());
			run_timer_ids[i].pop_front();
		}
		physics_mutex.unlock();
		render_system->mutex.unlock();
	}
}



