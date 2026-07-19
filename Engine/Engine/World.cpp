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

	//Serialization policies for the engine's own components. Registration is
	//idempotent and the descriptors are stateless, so running this once per
	//coordinator (scene and templates) is harmless.
	using ECS::ComponentPolicy;
	ECS::ComponentRegistry& registry = ECS::ComponentRegistry::Instance();
	//Identity and placement: every entity has them, nothing may take them away.
	registry.Register<Components::Base>(ComponentPolicy::Mandatory);
	registry.Register<Components::Transform>(ComponentPolicy::Mandatory);
	//Freely added and removed. Mesh and Material need an asset to exist at all, and
	//get GetDefaultMesh()/GetDefaultMaterial() when added from scratch; Bounds
	//measures itself from the entity's mesh. None of them is a one-way door.
	registry.Register<Components::Physics>(ComponentPolicy::Full);
	registry.Register<Components::Player>(ComponentPolicy::Full);
	registry.Register<Components::AmbientLight>(ComponentPolicy::Full);
	registry.Register<Components::DirectionalLight>(ComponentPolicy::Full);
	registry.Register<Components::PointLight>(ComponentPolicy::Full);
	registry.Register<Components::Sky>(ComponentPolicy::Full);
	registry.Register<Components::Mesh>(ComponentPolicy::Full);
	registry.Register<Components::Material>(ComponentPolicy::Full);
	registry.Register<Components::Bounds>(ComponentPolicy::Full);
	registry.Register<Components::Lighted>(ComponentPolicy::Full);
	//Visible in the Inspector, but not editable by hand: Camera is entirely derived
	//from its entity's Transform, and Particles owns emitter definitions that cannot
	//be rebuilt from JSON (a "remove" would discard them irrecoverably).
	registry.Register<Components::Camera>(ComponentPolicy::Locked);
	registry.Register<Components::Particles>(ComponentPolicy::Locked);
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

void World::SetPhysicsPause(bool paused) {
	physics_paused = paused;
}

bool World::GetPhysicsPause(void) const {
	return physics_paused;
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

Core::ShapeData*
World::GetEntityShape(const std::string& entity_name) {
	Core::ShapeData* shape = shapes.Get(entity_name);
	if (shape == nullptr) {
		//Clones have no shape of their own; they use their root source's.
		auto alias = clone_shape_alias.find(entity_name);
		if (alias != clone_shape_alias.end()) {
			shape = shapes.Get(alias->second);
		}
	}
	return shape;
}

void
World::AliasEntityShape(const std::string& old_name, const std::string& new_name) {
	if (old_name == new_name) {
		return;
	}
	//Resolve through any existing alias so a chain of renames still lands on the
	//FBX name the shape is actually stored under.
	std::string shape_name = old_name;
	auto alias = clone_shape_alias.find(old_name);
	if (alias != clone_shape_alias.end()) {
		shape_name = alias->second;
	}
	if (shapes.Get(shape_name) != nullptr) {
		clone_shape_alias[new_name] = shape_name;
	}
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

std::set<Entity> World::LoadFBX(const std::string& file, bool triangulate, bool relative,
	                Core::FlatMap<std::string, Core::MaterialData>& materials,
	                Core::FlatMap<std::string, Core::MeshData>& meshes,
	                Core::FlatMap<std::string, Core::ShapeData>& shapes,
	                ECS::Coordinator* c, Core::VertexBuffer<Core::Vertex>* vb, bool use_animation_names) {
	std::string full_path_file;
	std::set<Entity> entities;
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
			for (const auto& e : loader.ProcessEntity(meshes, materials, shapes, c, n))
			{
				entities.insert(e);
			}
		}
	}
	return entities;
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

const std::set<ECS::Entity>& World::GetTemplateEntities(const std::string& template_name) {
	return template_entities[template_name];
}

bool World::IsTemplateLoaded(const std::string& template_name) {
	return template_entities.find(template_name) != template_entities.end();
}

void World::LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names) {
	template_entities[std::filesystem::path(template_file).filename().replace_extension().string()] = LoadFBX(template_file, triangulate, relative, materials, meshes, shapes, templates_coordinator, vertex_buffer, use_animation_names);
}

void World::RefreshMeshBuffers() {
	vertex_buffer->Unprepare();
	vertex_buffer->Prepare();
	bvh_buffer->Unprepare();
	bvh_buffer->Clean();
	for (auto& m : meshes.GetData()) {
		bvh_buffer->Add(m.bvh.Root(), m.bvh.Size(), &m.bvhOffset);
	}
	bvh_buffer->Prepare();
}

Core::MaterialData* World::GetDefaultMaterial() {
	if (Core::MaterialData* existing = materials.Get(DEFAULT_MATERIAL_NAME)) {
		return existing;
	}
	//MaterialData's constructor already installs the standard render/shadow/depth
	//shaders, so a plain white material needs nothing but its colour set.
	//Insert first, Init second: MaterialData refuses to be copied once initialized,
	//and Insert copies into the collection.
	materials.Insert(DEFAULT_MATERIAL_NAME, Core::MaterialData{ DEFAULT_MATERIAL_NAME });
	Core::MaterialData* material = materials.Get(DEFAULT_MATERIAL_NAME);
	if (material == nullptr) {
		return nullptr;
	}
	material->props.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->props.ambientColor = { 0.1f, 0.1f, 0.1f, 1.0f };
	material->props.specIntensity = 0.2f;
	material->props.opacity = 1.0f;
	material->Init();
	return material;
}

Core::MeshData* World::GetDefaultMesh() {
	if (Core::MeshData* existing = meshes.Get(DEFAULT_MESH_NAME)) {
		return existing;
	}
	if (vertex_buffer == nullptr) {
		return nullptr;
	}

	//A unit cube centred on the origin: 24 vertices rather than 8, because each face
	//needs its own normal, tangent and UVs.
	std::vector<Core::Vertex> vertices;
	std::vector<uint32_t> indices;
	const float h = 0.5f;
	struct Face { float3 normal; float3 tangent; float3 corners[4]; };
	const Face faces[6] = {
		{ { 0, 0,-1}, {1,0,0}, { {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, { h,-h,-h} } }, //back
		{ { 0, 0, 1}, {-1,0,0}, { { h,-h, h}, { h, h, h}, {-h, h, h}, {-h,-h, h} } }, //front
		{ {-1, 0, 0}, {0,0,1}, { {-h,-h, h}, {-h, h, h}, {-h, h,-h}, {-h,-h,-h} } }, //left
		{ { 1, 0, 0}, {0,0,-1}, { { h,-h,-h}, { h, h,-h}, { h, h, h}, { h,-h, h} } }, //right
		{ { 0,-1, 0}, {1,0,0}, { {-h,-h, h}, {-h,-h,-h}, { h,-h,-h}, { h,-h, h} } }, //bottom
		{ { 0, 1, 0}, {1,0,0}, { {-h, h,-h}, {-h, h, h}, { h, h, h}, { h, h,-h} } }, //top
	};
	const float2 uvs[4] = { {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f} };
	for (const Face& face : faces) {
		uint32_t base = (uint32_t)vertices.size();
		for (int i = 0; i < 4; ++i) {
			Core::Vertex v;
			v.Position = face.corners[i];
			v.Normal = face.normal;
			v.Tangent = face.tangent;
			//Bitangent completes the frame: normal x tangent.
			v.Bitangent = {
				face.normal.y * face.tangent.z - face.normal.z * face.tangent.y,
				face.normal.z * face.tangent.x - face.normal.x * face.tangent.z,
				face.normal.x * face.tangent.y - face.normal.y * face.tangent.x
			};
			v.UV = uvs[i];
			v.MeshUV = uvs[i];
			vertices.push_back(v);
		}
		indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
	}

	//Insert before Init for the same reason as the material above.
	meshes.Insert(DEFAULT_MESH_NAME, Core::MeshData{});
	Core::MeshData* mesh = meshes.Get(DEFAULT_MESH_NAME);
	if (mesh == nullptr) {
		return nullptr;
	}
	mesh->Init(vertex_buffer, DEFAULT_MESH_NAME, vertices, indices, nullptr);
	//The GPU vertex/BVH buffers were uploaded once during Init(); a mesh created
	//after that (which is every default mesh, since it is made on demand) is not in
	//them yet and would draw as nothing.
	if (scene_init) {
		RefreshMeshBuffers();
	}
	return mesh;
}

bool World::IsComponentRemoved(ECS::Entity e, const std::string& component) const {
	auto it = removed_components.find(e);
	return it != removed_components.end() && it->second.count(component) != 0;
}

ECS::SerializeContext World::MakeSerializeContext() {
	ECS::SerializeContext ctx;
	ctx.world = this;
	ctx.coordinator = coordinator;
	return ctx;
}

void World::ApplyComponents(ECS::Entity e, const nlohmann::json& entry) {
	if (e == ECS::INVALID_ENTITY_ID) {
		return;
	}
	ECS::SerializeContext ctx = MakeSerializeContext();
	const ECS::ComponentRegistry& registry = ECS::ComponentRegistry::Instance();

	if (entry.contains("remove") && entry["remove"].is_array()) {
		for (const auto& name_json : entry["remove"]) {
			if (!name_json.is_string()) {
				continue;
			}
			const std::string name = name_json;
			const ECS::ComponentDesc* desc = registry.Find(name);
			if (desc == nullptr) {
				printf("World::ApplyComponents: unknown component '%s' in \"remove\", skipping.\n",
					name.c_str());
				continue;
			}
			if (!desc->Removable()) {
				//Base/Transform are what make an entity addressable and placeable;
				//honouring this would leave the scene with an entity nothing can find
				//or draw.
				printf("World::ApplyComponents: component '%s' cannot be removed, skipping.\n",
					name.c_str());
				continue;
			}
			desc->remove(ctx, e);
			//Remembered so the default-component passes in Init() do not hand it
			//straight back.
			removed_components[e].insert(name);
		}
	}

	if (entry.contains("components") && entry["components"].is_object()) {
		for (const auto& [name, value] : entry["components"].items()) {
			const ECS::ComponentDesc* desc = registry.Find(name);
			if (desc == nullptr) {
				//Expected whenever a level is opened by a binary that doesn't define the
				//component (the Scene Editor on a game level). The editor preserves these
				//blocks verbatim on save, so skipping here loses nothing.
				printf("World::ApplyComponents: unknown component '%s', skipping.\n", name.c_str());
				continue;
			}
			desc->apply(ctx, e, value);
		}
	}
}

void World::ParsePhysicsJson(const nlohmann::json& physics_json, Components::Physics& physics) {
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
}

ECS::Entity World::SpawnInstance(const std::string& name, const std::string& template_name,
	const float3& position, const float4& rotation, const float3& scale,
	const std::string& material_name, const nlohmann::json* physics_json,
	std::vector<ECS::Entity>* out_parts)
{
	ECS::Entity primary = ECS::INVALID_ENTITY_ID;
	const std::set<ECS::Entity>& parts = GetTemplateEntities(template_name);
	assert(!parts.empty() && "SpawnInstance: unknown template name.");

	int part_index = 0;
	for (ECS::Entity te : parts) {
		//Skinned FBX templates can register non-renderable nodes (armatures, empties)
		//alongside their meshes; those have no Mesh/Bounds to clone, and asking for
		//them throws. Skip them - the part keeps its index so multi-part instance
		//names stay stable regardless of where the non-mesh nodes sort.
		if (!templates_coordinator->ContainsComponent<Components::Mesh>(te) ||
			!templates_coordinator->ContainsComponent<Components::Bounds>(te) ||
			!templates_coordinator->ContainsComponent<Components::Transform>(te)) {
			++part_index;
			continue;
		}
		const Components::Bounds& tbounds = templates_coordinator->GetConstComponent<Components::Bounds>(te);
		const Components::Transform& tt = templates_coordinator->GetConstComponent<Components::Transform>(te);

		//Multi-part templates (several FBX nodes sharing the same template name) get a
		//stable "_<index>" suffix so each part keeps a unique, reload-safe name.
		std::string instance_name = (parts.size() > 1) ? (name + "_" + std::to_string(part_index)) : name;
		ECS::Entity e = coordinator->CreateEntity(instance_name);

		coordinator->AddComponent<Components::Base>(e, Components::Base{ .name = instance_name, .id = e, .draw_method = Components::eDrawMethod::DRAW_SCREEN });
		coordinator->AddComponent<Components::Bounds>(e, tbounds);
		coordinator->AddComponent<Components::Mesh>(e);
		coordinator->AddComponent<Components::Lighted>(e);
		coordinator->AddComponent<Components::Material>(e);

		Components::Mesh& mesh = coordinator->GetComponent<Components::Mesh>(e);
		mesh.SetData(templates_coordinator->GetComponent<Components::Mesh>(te).GetData());

		Components::Material& mat = coordinator->GetComponent<Components::Material>(e);
		mat.data = templates_coordinator->GetComponent<Components::Material>(te).data;
		if (!material_name.empty()) {
			Core::MaterialData* named = materials.Get(material_name);
			if (named != nullptr) {
				mat.data = named;
			}
		}

		Components::Transform t{};
		t.position = ADD_F3_F3(position, tt.position);
		t.rotation = quaternion_multiply(rotation, tt.rotation);
		t.scale = { tt.scale.x * scale.x, tt.scale.y * scale.y, tt.scale.z * scale.z };
		t.dirty = true;
		coordinator->AddComponent<Components::Transform>(e, t);

		if (physics_json != nullptr) {
			Components::Physics physics;
			ParsePhysicsJson(*physics_json, physics);
			coordinator->AddComponent<Components::Physics>(e, physics);
			Components::Physics& p = coordinator->GetComponent<Components::Physics>(e);
			p.Init(phys_world, p.type, nullptr, tbounds.bounding_box.Extents, t.position, t.scale, t.rotation, p.shape);
		}

		coordinator->NotifySignatureChange(e);

		if (out_parts != nullptr) {
			out_parts->push_back(e);
		}
		if (primary == ECS::INVALID_ENTITY_ID) {
			primary = e;
		}
		++part_index;
	}
	return primary;
}

ECS::Entity World::CloneEntity(const std::string& new_name, const std::string& source_name)
{
	ECS::Entity src = coordinator->GetEntityByName(source_name);
	if (src == ECS::INVALID_ENTITY_ID) {
		printf("World::CloneEntity: source entity not found: %s\n", source_name.c_str());
		return ECS::INVALID_ENTITY_ID;
	}
	if (coordinator->GetEntityByName(new_name) != ECS::INVALID_ENTITY_ID) {
		printf("World::CloneEntity: entity name already exists: %s\n", new_name.c_str());
		return ECS::INVALID_ENTITY_ID;
	}
	//Only mesh entities are clonable: lights, cameras and the sky own live GPU or
	//system resources a plain component copy would alias.
	if (!coordinator->ContainsComponent<Components::Base>(src) ||
		!coordinator->ContainsComponent<Components::Transform>(src) ||
		!coordinator->ContainsComponent<Components::Bounds>(src) ||
		!coordinator->ContainsComponent<Components::Mesh>(src)) {
		printf("World::CloneEntity: %s is not a mesh entity, not cloned.\n", source_name.c_str());
		return ECS::INVALID_ENTITY_ID;
	}

	ECS::Entity e = coordinator->CreateEntity(new_name);

	Components::Base base = coordinator->GetConstComponent<Components::Base>(src);
	base.name = new_name;
	base.id = e;
	base.creation_time = Core::Scheduler::GetNanoSeconds();
	coordinator->AddComponent<Components::Base>(e, base);

	Components::Transform t = coordinator->GetConstComponent<Components::Transform>(src);
	t.dirty = true;
	coordinator->AddComponent<Components::Transform>(e, t);

	const Components::Bounds& bounds = coordinator->GetConstComponent<Components::Bounds>(src);
	coordinator->AddComponent<Components::Bounds>(e, bounds);

	coordinator->AddComponent<Components::Mesh>(e);
	coordinator->GetComponent<Components::Mesh>(e).SetData(coordinator->GetComponent<Components::Mesh>(src).GetData());

	if (coordinator->ContainsComponent<Components::Material>(src)) {
		coordinator->AddComponent<Components::Material>(e, coordinator->GetConstComponent<Components::Material>(src));
	}
	coordinator->AddComponent<Components::Lighted>(e);

	//Mesh-shape colliders are looked up by the original FBX entity name; remember
	//the clone's root source so Init() (for load-time clones) and the runtime path
	//below resolve the same shape. Chains (clone of a clone) collapse to the root.
	auto alias_it = clone_shape_alias.find(source_name);
	const std::string& shape_name = (alias_it != clone_shape_alias.end()) ? alias_it->second : source_name;
	clone_shape_alias[new_name] = shape_name;

	if (coordinator->ContainsComponent<Components::Physics>(src)) {
		const Components::Physics& sp = coordinator->GetConstComponent<Components::Physics>(src);
		Components::Physics p;
		p.type = sp.type;
		p.shape = sp.shape;
		p.bounce = sp.bounce;
		p.friction = sp.friction;
		p.air_friction = sp.air_friction;
		coordinator->AddComponent<Components::Physics>(e, p);
		if (scene_init) {
			//Load-time clones get their body from the Init() pass like every other
			//mesh entity; runtime clones must create theirs here.
			Components::Physics& np = coordinator->GetComponent<Components::Physics>(e);
			ShapeData* shape = nullptr;
			if (np.type != reactphysics3d::BodyType::DYNAMIC) {
				shape = shapes.Get(shape_name);
			}
			np.Init(phys_world, np.type, shape, bounds.bounding_box.Extents, t.position, t.scale, t.rotation, np.shape);
		}
	}

	coordinator->NotifySignatureChange(e);
	return e;
}

void World::LoadInstances(const nlohmann::json& instances_json) {
	for (const auto& instance : instances_json) {
		std::string name = instance["name"];
		std::string template_name = instance["template"];

		float3 position{};
		if (instance.contains("position")) {
			const auto& pos = instance["position"];
			position = { pos["x"], pos["y"], pos["z"] };
		}
		float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		if (instance.contains("rotation")) {
			const auto& rot = instance["rotation"];
			rotation = { rot["x"], rot["y"], rot["z"], rot["w"] };
		}
		float3 scale{ 1.0f, 1.0f, 1.0f };
		if (instance.contains("scale")) {
			const auto& scl = instance["scale"];
			scale = { scl["x"], scl["y"], scl["z"] };
		}
		std::string material_name = instance.value("material", "");
		const nlohmann::json* physics_json = instance.contains("physics") ? &instance["physics"] : nullptr;

		//Per-instance component overrides are what keep two instances of one template
		//independent: the template decides the component set every instance starts
		//with, and this block is the delta for *this* one. Applied to every part, so a
		//multi-part template's override does not land on part 0 alone.
		std::vector<ECS::Entity> parts;
		SpawnInstance(name, template_name, position, rotation, scale, material_name,
			physics_json, &parts);
		for (ECS::Entity part : parts) {
			ApplyComponents(part, instance);
		}
	}
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
		//The base level FBX is optional: a project can start from an empty,
		//instances-only scene (see "instances" below) with no authored geometry yet.
		if (jw.contains("level")) {
			json& world_fbx = jw["level"];
			std::string file = world_fbx["file"];
			bool triangulate = world_fbx["triangulate"];
			LoadFBX(file, triangulate, true, materials, meshes, shapes, coordinator, vertex_buffer);
		}
		if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 5.0f * progress_unit); }

		//Load templates
		if (jw.contains("templates")) {
			auto& template_files = jw["templates"];
			for (json& t : template_files) {
				LoadTemplate(t["file"], t["triangulate"], true);
			}
			coordinator->SendEvent(this, EVENT_ID_TEMPLATES_LOADED);
			if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }
		}

		//Load editor-placed object instances (entities cloned from templates at load time,
		//as opposed to "entities" below which only modifies already-existing named entities)
		if (jw.contains("instances")) {
			LoadInstances(jw["instances"]);
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

		//Complete entities information.
		//
		//Entries are keyed by name, and GetEntitiesByName pattern-matches, so
		//{"name": "Cube*"} is a rule covering every entity whose name starts with
		//"Cube". That is how a level gives a whole family of entities the same
		//material or physics in one line - but it also means a rule cannot, on its
		//own, express "all of them except this one".
		//
		//Hence two passes: every wildcard rule is applied first, then every
		//exact-name entry, regardless of the order they appear in the file. An exact
		//entry therefore always wins over the wildcard rules that also matched its
		//entity, which is what makes per-entity overrides work - a
		//{"name": "Cube.001", "remove": ["Physics"]} entry strips what "Cube*" just
		//granted, and touches no other Cube.
		for (int pass = 0; pass < 2; ++pass) {
			const bool wildcard_pass = (pass == 0);
			for (auto& entity : jw["entities"]) {
				std::string name = entity["name"];
				if ((name.find('*') != std::string::npos) != wildcard_pass) {
					continue;
				}
				std::list<ECS::Entity> entity_list = coordinator->GetEntitiesByName(name);
				if (entity_list.empty()) {
					//Not fatal: an "entities" rule matching nothing is already silently
					//tolerated in Release builds (this used to be a hard assert here, live
					//only in Debug); log it instead so Debug builds behave the same way,
					// just visibly instead of invisibly.
					printf("World::Load: warning: no entity matches name pattern '%s', skipping rule.\n", name.c_str());
					continue;
				}
				for (ECS::Entity e : entity_list) {
					assert(e != ECS::INVALID_ENTITY_ID && "Unknown entity.");
					ApplyComponents(e, entity);
					//Editor renames: the entry is keyed by the authored (FBX/lights) name,
					//"rename" carries the name the user gave it. Exact names only - a
					//wildcard rule renaming several entities to one name cannot work.
					if (entity.contains("rename") && !wildcard_pass) {
						std::string new_name = entity["rename"];
						if (!new_name.empty() && new_name != name &&
							coordinator->GetEntityByName(new_name) == ECS::INVALID_ENTITY_ID) {
							coordinator->ChangeEntityName(name, new_name);
							coordinator->GetComponent<Components::Base>(e).name = new_name;
							coordinator->NotifySignatureChange(e);
						}
					}
				}
			}
		}

		//Editor-created entity copies: clones of already-loaded scene entities, with
		//their own transform. Processed after "entities" so renames are already
		//applied, and before "removed_entities" so a copy of a since-deleted entity
		//can still clone it.
		if (jw.contains("clones")) {
			for (const auto& clone : jw["clones"]) {
				std::string clone_name = clone["name"];
				std::string source_name = clone["source"];
				ECS::Entity e = CloneEntity(clone_name, source_name);
				if (e == ECS::INVALID_ENTITY_ID) {
					continue;
				}
				Components::Transform& t = coordinator->GetComponent<Components::Transform>(e);
				if (clone.contains("position")) {
					const auto& pos = clone["position"];
					t.position = { pos["x"], pos["y"], pos["z"] };
				}
				if (clone.contains("rotation")) {
					const auto& rot = clone["rotation"];
					t.rotation = { rot["x"], rot["y"], rot["z"], rot["w"] };
				}
				if (clone.contains("scale")) {
					const auto& scl = clone["scale"];
					t.scale = { scl["x"], scl["y"], scl["z"] };
				}
				t.dirty = true;
				//Per-clone component overrides, applied after the transform so a
				//"components": {"Transform": ...} block still has the last word.
				ApplyComponents(e, clone);
			}
		}

		//Entities the editor deleted (cut). Removed last so they were still available
		//as clone sources above.
		if (jw.contains("removed_entities")) {
			for (const auto& removed : jw["removed_entities"]) {
				std::string removed_name = removed;
				ECS::Entity e = coordinator->GetEntityByName(removed_name);
				if (e != ECS::INVALID_ENTITY_ID) {
					coordinator->DestroyEntity(e);
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
				//Mesh entities get a static body by default - unless the level said to
				//take it away. Without this check the default would quietly reverse
				//every "remove": ["Physics"] the loader had just applied.
				if (IsComponentRemoved(e.second, Components::Physics::NAME)) {
					continue;
				}
				coordinator->AddComponent(e.second, Components::Physics{});
				coordinator->NotifySignatureChange(e.second);
			}
			Components::Physics& p = coordinator->GetComponent<Components::Physics>(e.second);
			Components::Transform& t = coordinator->GetComponent<Components::Transform>(e.second);
			Components::Bounds& b = coordinator->GetComponent<Components::Bounds>(e.second);
			Components::Base& base = coordinator->GetComponent<Components::Base>(e.second);
			if (p.type != reactphysics3d::BodyType::DYNAMIC) {
				//Dynamic bodies use capsules, can't use mesh shape
				shape = GetEntityShape(e.first);
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
	scene_init = true;
}

void World::Run(int render_fps, int background_fps, int physics_fps, bool auto_render) {
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

			if (auto_render) {
				run_timer_ids[DXCore::MAIN_THREAD].push_back(Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / render_fps, [this](const Scheduler::TimerData& t) {
					//Update render system that don't need sync with lockstep
					particle_system->Update(t.period, t.total);
					render_system->Update();
					render_system->mutex.lock();
					coordinator->SendEvent(this, World::EVENT_ID_UPDATE_MAIN);
					render_system->mutex.unlock();
					return true;
					}));
			}

			run_timer_ids[DXCore::BACKGROUND_THREAD].push_back(Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(background_thread_period, [this](const Scheduler::TimerData& t) {
				//Update systems that need sync with lockstep
				if (lockstep_sync) {
					while (current_background_thread_nsec >= current_server_nsec) { Sleep(1); }
				}
				render_system->mutex.lock();
				physics_mutex.lock();
				//physics systems moves information to transform component used by the renderer,
				//so we need to take the renderer lock for this
				if (!physics_paused) {
					phys_world->update((float)t.period / 1000000000.0f);
					physics_system->Update(t.period, t.total, false);
				}
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

