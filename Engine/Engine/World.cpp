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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include "World.h"
#include <Network/LockStepClient.h>
#include <Core/PhysicsCommon.h>
#include <Core/MeshSimplify.h>
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
	//Registered last, deliberately. RegisterComponent hands out sequential type ids
	//and a signature is a bitmask over them, so inserting a component in the middle
	//of this list renumbers every one after it. Nothing should depend on the actual
	//numbers - signatures are built at runtime from GetComponentType<T>() - but a
	//new component is not the place to find out that something does. Appending keeps
	//every existing id exactly where it was.
	c->RegisterComponent<Components::SplatCloud>();

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
	//Like Mesh: needs an asset to exist at all, and gets GetDefaultSplatCloud() when
	//added from scratch, so adding and removing it are both honest operations.
	registry.Register<Components::SplatCloud>(ComponentPolicy::Full);
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
	splat_cloud_system = RegisterSystem<Systems::SplatCloudSystem>();
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

Core::FlatMap<std::string, Core::SplatCloudData>&
World::GetSplatClouds() {
	return splat_clouds;
}

Core::SplatCloudData*
World::LoadSplatCloud(const std::string& file, const std::string& name) {
	//Re-registering a name is a no-op rather than a re-read: a level naming the same
	//cloud from two places (a template and an instance override) is normal, and
	//re-importing would throw away the GPU buffer every user is pointing at.
	if (Core::SplatCloudData* existing = splat_clouds.Get(name)) {
		return existing;
	}
	//Inserted empty and then filled through the stored pointer, never loaded into a
	//local and copied in: once Prepare has run a SplatCloudData owns a COM buffer and
	//an SRV, and FlatMap::Insert takes a copy, so a prepared temporary would release
	//the very resources the stored copy is holding when it went out of scope.
	splat_clouds.Insert(name, Core::SplatCloudData{});
	Core::SplatCloudData* stored = splat_clouds.Get(name);
	if (stored == nullptr || !stored->Load(file, name)) {
		//A failed load leaves the empty entry behind on purpose. FlatMap removal
		//relocates another element (the reason RemoveMaterial retires rather than
		//erases), which would dangle every SplatCloud::data pointing at it; an empty
		//cloud simply draws nothing.
		return nullptr;
	}
	if (FAILED(stored->Prepare())) {
		LOG_ERROR("World::LoadSplatCloud: '%s' loaded but failed to upload", name.c_str());
	}
	return stored;
}

Core::SplatCloudData* World::GetDefaultSplatCloud() {
	if (Core::SplatCloudData* existing = splat_clouds.Get(DEFAULT_SPLAT_CLOUD_NAME)) {
		return existing;
	}
	//A sphere of splats, coloured by normal. Built rather than loaded so a SplatCloud
	//component is never an invisible entity, and so the automation suite has a fixture
	//that needs no binary asset checked into the repo.
	splat_clouds.Insert(DEFAULT_SPLAT_CLOUD_NAME, Core::SplatCloudData{});
	Core::SplatCloudData* stored = splat_clouds.Get(DEFAULT_SPLAT_CLOUD_NAME);
	if (stored != nullptr) {
		stored->BuildDefault(DEFAULT_SPLAT_CLOUD_NAME);
		stored->Prepare();
	}
	return stored;
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
		//The sun is built here rather than through DirectionalLight::FromJson, so the
		//cascade keys have to be read explicitly or a sky's sun would silently never
		//get the cascade set its level asked for.
		Components::DirectionalLight::CascadeSettings cascades;
		cascades.FromJson(light);
		directional.Init(ColorRGBFromStr(light["color"]), direction, light["cast_shadow"], light["resolution"], light["density"], cascades);
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

		//Remember the file and the texture root it declares, so an editor can write
		//these materials back where they came from.
		material_files[file] = root;

		auto& materials = GetMaterials();
		for (const auto& m : scene["materials"]) {
			std::string name = m["name"];
			MaterialData* mdata = materials.Get(name);
			if (mdata == nullptr) {
				materials.Insert(name, MaterialData{ name });
				mdata = materials.Get(name);
			}
			mdata->Load(root, m.dump());
			material_origin[name] = file;
		}

		//The file's layer stacks. Read after the materials because every layer names
		//one of them, and skipping an entry that carries no "textures" array is not
		//defensive coding for its own sake: Tools/MaterialDesigner has been writing
		//plain materials into this array, and reading one as a stack would register a
		//multi-material with no layers under a material's name.
		if (scene.contains("multi_materials")) {
			for (const auto& mm : scene["multi_materials"]) {
				if (!mm.contains("textures") || !mm.contains("name")) {
					continue;
				}
				const std::string name = mm["name"];
				LoadMultiMaterial(name, mm);
				multi_material_origin[name] = file;
			}
		}
	}
	//A material may name a stack this file declares below it, or one another file
	//declared - so binding is a pass of its own, after everything is in.
	ResolveMultiMaterials();
}

std::string World::GetMaterialOrigin(const std::string& material_name) const {
	auto it = material_origin.find(material_name);
	return it != material_origin.end() ? it->second : std::string();
}

bool World::SetMaterialOrigin(const std::string& material_name, const std::string& mat_file) {
	if (material_files.find(mat_file) == material_files.end()) {
		return false;
	}
	if (materials.Get(material_name) == nullptr) {
		return false;
	}
	material_origin[material_name] = mat_file;
	return true;
}

Core::MaterialData* World::CreateMaterial(const std::string& name, const std::string& mat_file) {
	if (name.empty()) {
		return nullptr;
	}
	if (material_files.find(mat_file) == material_files.end()) {
		return nullptr;
	}
	//A retired name is free to reuse: its MaterialData is still there (RemoveMaterial
	//never erases it), so revive that entry rather than colliding with it.
	const bool reviving = IsMaterialRemoved(name);
	if (!reviving && materials.Get(name) != nullptr) {
		return nullptr;
	}
	if (reviving) {
		removed_materials.erase(name);
	}
	else {
		//Insert first, Init second: MaterialData refuses to be copied once initialized
		//and Insert copies into the collection (same order as GetDefaultMaterial).
		materials.Insert(name, Core::MaterialData{ name });
	}
	Core::MaterialData* material = materials.Get(name);
	if (material == nullptr) {
		return nullptr;
	}
	material->source_json = nlohmann::json::object();
	material->texture_names = Core::MaterialTextures{};
	material->props.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->props.specIntensity = 0.2f;
	material->props.opacity = 1.0f;
	material->Init();
	material_origin[name] = mat_file;
	return material;
}

bool World::RemoveMaterial(const std::string& name) {
	if (materials.Get(name) == nullptr || IsMaterialRemoved(name)) {
		return false;
	}
	//Retired, not erased. MaterialData lives in a FlatMap, which fills the hole left
	//by a removal with the collection's last element - so physically removing one
	//material relocates another, and every Material component and render-tree key
	//still pointing at the moved one becomes a dangling pointer. Since callers have
	//only repointed the entities that used *this* material, that would corrupt an
	//unrelated one. Keeping the data alive and marking the name retired costs one
	//unused material per removal and cannot dangle; the same reasoning is why the
	//editor parks cut entities instead of destroying them.
	removed_materials.insert(name);
	material_origin.erase(name);
	return true;
}

bool World::IsMaterialRemoved(const std::string& name) const {
	return removed_materials.find(name) != removed_materials.end();
}

bool World::RestoreMaterial(const std::string& name, const std::string& mat_file) {
	if (!IsMaterialRemoved(name) || materials.Get(name) == nullptr) {
		return false;
	}
	if (material_files.find(mat_file) == material_files.end()) {
		return false;
	}
	removed_materials.erase(name);
	material_origin[name] = mat_file;
	return true;
}

bool World::SaveMaterialFile(const std::string& mat_file) {
	auto file_it = material_files.find(mat_file);
	if (file_it == material_files.end()) {
		return false;
	}
	const std::string& root = file_it->second;
	const std::string file_path = path + mat_file;

	nlohmann::json out;
	//"root" is stored relative to the assets path, exactly as the file declared it.
	out["root"] = root.compare(0, path.size(), path) == 0 ? root.substr(path.size()) : root;
	out["materials"] = nlohmann::json::array();
	//Sorted by name so the file has a stable order and diffs stay readable, rather
	//than following the material collection's internal layout.
	std::vector<std::string> names;
	for (const auto& origin : material_origin) {
		if (origin.second == mat_file) {
			names.push_back(origin.first);
		}
	}
	std::sort(names.begin(), names.end());
	for (const std::string& name : names) {
		Core::MaterialData* m = materials.Get(name);
		if (m != nullptr) {
			out["materials"].push_back(m->Save(root));
		}
	}

	//The layer stacks assigned to this file, same ordering rule as the materials.
	out["multi_materials"] = nlohmann::json::array();
	std::vector<std::string> stacks;
	for (const auto& origin : multi_material_origin) {
		if (origin.second == mat_file && !IsMultiMaterialRemoved(origin.first)) {
			stacks.push_back(origin.first);
		}
	}
	std::sort(stacks.begin(), stacks.end());
	for (const std::string& name : stacks) {
		auto it = multi_materials.find(name);
		if (it != multi_materials.end()) {
			out["multi_materials"].push_back(it->second.ToJson());
		}
	}

	std::ofstream stream(file_path);
	if (!stream.is_open()) {
		printf("World::SaveMaterialFile: cannot write %s\n", file_path.c_str());
		return false;
	}
	stream << out.dump(4);
	return stream.good();
}

bool World::SetMaterialShaders(const std::string& material_name,
	const Core::MaterialShaderNames& names) {
	auto c = GetCoordinator();
	Core::MaterialData* material = materials.Get(material_name);
	if (c == nullptr || material == nullptr) {
		return false;
	}
	auto rs = c->GetSystem<RenderSystem>();
	std::scoped_lock l(rs->mutex);
	if (!material->SetShaders(names)) {
		return false;
	}
	//Every entity drawing with this material is filed in the render trees under the
	//old shader tuple; leaving them there would draw each one twice (see
	//RenderSystem::RefreshDrawable).
	for (const auto& entry : c->GetEntites()) {
		const ECS::Entity e = entry.second;
		if (c->ContainsComponent<Components::Material>(e) &&
			c->GetComponent<Components::Material>(e).data == material) {
			rs->RefreshDrawable(e);
		}
	}
	return true;
}

bool World::SetEntityMaterial(ECS::Entity e, const std::string& material_name) {
	auto c = GetCoordinator();
	if (c == nullptr || !c->ContainsComponent<Components::Material>(e)) {
		return false;
	}
	Core::MaterialData* data = materials.Get(material_name);
	if (data == nullptr) {
		return false;
	}
	std::scoped_lock l(c->GetSystem<RenderSystem>()->mutex);
	c->GetComponent<Components::Material>(e).data = data;
	//The render system keys its draw trees by MaterialData pointer, so assigning the
	//pointer is not enough - the entity has to be re-registered under the new key.
	//AddDrawable evicts it from every other material bucket as it goes, so this both
	//adds the new entry and drops the stale one.
	c->NotifySignatureChange(e);
	return true;
}

std::vector<std::string> World::ListMultiMaterials() const {
	std::vector<std::string> names;
	for (const auto& entry : multi_materials) {
		if (!IsMultiMaterialRemoved(entry.first)) {
			names.push_back(entry.first);
		}
	}
	return names; //std::map already orders them by name
}

Core::MultiMaterialData* World::GetMultiMaterial(const std::string& name) {
	auto it = multi_materials.find(name);
	if (it == multi_materials.end() || IsMultiMaterialRemoved(name)) {
		return nullptr;
	}
	return &it->second;
}

void World::SetMultiMaterial(const std::string& name, const Core::MultiMaterialData& data) {
	//The render thread walks these arrays every frame, so an in-place edit has to
	//happen under the render lock like every other material change.
	auto c = GetCoordinator();
	auto rs = (c != nullptr) ? c->GetSystem<RenderSystem>() : nullptr;
	std::unique_lock<std::recursive_mutex> l;
	if (rs != nullptr) {
		l = std::unique_lock<std::recursive_mutex>(rs->mutex);
	}
	Core::MultiMaterialData& stored = multi_materials[name];
	//A replaced stack keeps its own name: the caller may have edited a copy that was
	//taken before a rename, and the key is what everything else resolves through.
	stored = data;
	stored.name = name;
	stored.Rebuild(path, materials);
	removed_multi_materials.erase(name);
	//Rebinding matters even for a pure layer edit: a material holds a pointer into
	//this map, and inserting a new key can rehash nothing here (std::map is stable)
	//but a *new* stack has to be picked up by the materials naming it.
	ResolveMultiMaterials();
}

void World::LoadMultiMaterial(const std::string& name, const nlohmann::json& multi_material_info) {
	Core::MultiMaterialData& stored = multi_materials[name];
	stored.FromJson(multi_material_info, path, materials);
	//The registry key wins over the record's own "name", after the fact: a file whose
	//entry disagrees with the key it was filed under would otherwise save itself back
	//under a name nothing resolves.
	stored.name = name;
	removed_multi_materials.erase(name);
}

Core::MultiMaterialData* World::CreateMultiMaterial(const std::string& name, const std::string& mat_file) {
	if (name.empty() || material_files.find(mat_file) == material_files.end()) {
		return nullptr;
	}
	const bool reviving = IsMultiMaterialRemoved(name);
	if (!reviving && multi_materials.find(name) != multi_materials.end()) {
		return nullptr;
	}
	if (reviving) {
		removed_multi_materials.erase(name);
	}
	else {
		Core::MultiMaterialData fresh;
		fresh.name = name;
		multi_materials[name] = fresh;
	}
	multi_material_origin[name] = mat_file;
	return &multi_materials[name];
}

bool World::RemoveMultiMaterial(const std::string& name) {
	if (multi_materials.find(name) == multi_materials.end() || IsMultiMaterialRemoved(name)) {
		return false;
	}
	//Detach it everywhere first: a material left naming a retired stack would keep
	//drawing with layers that are no longer part of the project and would write the
	//dangling name back out on the next save.
	for (auto& material : materials.GetData()) {
		if (material.multi_material_name == name) {
			SetMaterialMultiMaterial(material.name, std::string());
		}
	}
	removed_multi_materials.insert(name);
	multi_material_origin.erase(name);
	return true;
}

bool World::RestoreMultiMaterial(const std::string& name, const std::string& mat_file) {
	if (!IsMultiMaterialRemoved(name) || multi_materials.find(name) == multi_materials.end()) {
		return false;
	}
	if (material_files.find(mat_file) == material_files.end()) {
		return false;
	}
	removed_multi_materials.erase(name);
	multi_material_origin[name] = mat_file;
	return true;
}

bool World::IsMultiMaterialRemoved(const std::string& name) const {
	return removed_multi_materials.find(name) != removed_multi_materials.end();
}

std::string World::GetMultiMaterialOrigin(const std::string& name) const {
	auto it = multi_material_origin.find(name);
	return it != multi_material_origin.end() ? it->second : std::string();
}

bool World::SetMaterialMultiMaterial(const std::string& material_name,
	const std::string& multi_material_name) {
	Core::MaterialData* material = materials.Get(material_name);
	if (material == nullptr) {
		return false;
	}
	if (!multi_material_name.empty() && GetMultiMaterial(multi_material_name) == nullptr) {
		return false;
	}
	auto c = GetCoordinator();
	auto rs = (c != nullptr) ? c->GetSystem<RenderSystem>() : nullptr;
	std::unique_lock<std::recursive_mutex> l;
	if (rs != nullptr) {
		l = std::unique_lock<std::recursive_mutex>(rs->mutex);
	}
	material->multi_material_name = multi_material_name;
	material->multi_material = multi_material_name.empty()
		? nullptr : GetMultiMaterial(multi_material_name);
	//Attaching a stack changes which textures and constants the draw needs, and the
	//tessellation/displacement values come from the stack rather than the material -
	//so the entities have to be re-registered, exactly as for a shader change.
	if (rs != nullptr) {
		for (const auto& entry : c->GetEntites()) {
			const ECS::Entity e = entry.second;
			if (c->ContainsComponent<Components::Material>(e) &&
				c->GetComponent<Components::Material>(e).data == material) {
				rs->RefreshDrawable(e);
			}
		}
	}
	return true;
}

void World::ResolveMultiMaterials() {
	for (auto& entry : multi_materials) {
		entry.second.Rebuild(path, materials);
	}
	for (auto& material : materials.GetData()) {
		material.multi_material = material.multi_material_name.empty()
			? nullptr : GetMultiMaterial(material.multi_material_name);
		if (!material.multi_material_name.empty() && material.multi_material == nullptr) {
			LOG_WARN("World: material '%s' names multi-material '%s', which this level does "
				"not declare; drawing it as a plain material",
				material.name.c_str(), material.multi_material_name.c_str());
		}
	}
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

//The names in a world collection, and the ones a load added to it. Used to work
//out what one imported file contributed (see LoadModel).
template<class T>
static std::set<std::string> KeySet(const Core::FlatMap<std::string, T>& map) {
	std::vector<std::string> keys = map.Keys();
	return std::set<std::string>(keys.begin(), keys.end());
}

template<class T>
static std::vector<std::string> NewKeys(const std::set<std::string>& before,
	const Core::FlatMap<std::string, T>& after) {
	std::vector<std::string> added;
	for (const std::string& key : after.Keys()) {
		if (before.count(key) == 0) {
			added.push_back(key);
		}
	}
	std::sort(added.begin(), added.end());
	return added;
}

const std::set<ECS::Entity>& World::GetTemplateEntities(const std::string& template_name) {
	//Deliberately not operator[]: asking about a template that does not exist must
	//not register an empty one under that name, or a single typo in a level's
	//"instances" section would make the name show up as a real (but unusable)
	//template in IsTemplateLoaded and ListTemplates from then on.
	static const std::set<ECS::Entity> none;
	auto it = template_entities.find(template_name);
	if (it != template_entities.end()) {
		return it->second;
	}
	//Legacy compatibility, and only that: before models and templates were separate
	//things, importing an .fbx registered it as a placeable template, so a level of
	//that vintage has instances naming a model. Resolving those to the model's own
	//entities keeps such a level loading exactly as it did (see the header).
	auto model = model_entities.find(template_name);
	return (model != model_entities.end()) ? model->second : none;
}

bool World::IsTemplateLoaded(const std::string& template_name) {
	return template_entities.find(template_name) != template_entities.end();
}

void World::LoadModel(const std::string& model_file, bool triangulate, bool relative,
	bool use_animation_names) {
	const std::string name = std::filesystem::path(model_file).filename().replace_extension().string();

	//A Gaussian splat cloud is a model file like any other as far as the three asset
	//layers are concerned: it registers under its file stem, contributes an asset,
	//and creates no template - so File/Import Model, the Asset Browser's Models
	//section and "Create Template" all work on it unchanged. Only the reader differs,
	//because there is no FBX in it to read.
	std::string ext = std::filesystem::path(model_file).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	if (ext == ".ply") {
		if (IsModelLoaded(name)) {
			//Same dedup contract as the FBX path below: asking twice must not
			//unregister what the first call loaded.
			return;
		}
		if (LoadSplatCloud(model_file, name) == nullptr) {
			return;
		}
		ModelAssets splat_assets;
		splat_assets.file = model_file;
		splat_assets.triangulate = triangulate;
		splat_assets.splat_clouds.push_back(name);
		model_assets[name] = std::move(splat_assets);
		//Registered with an empty entity set. A .ply contributes no nodes, which is
		//exactly what an animation-only .fbx does too, and GetTemplateEntities'
		//fallback already copes with a model that spawns nothing by itself.
		model_entities[name] = {};
		return;
	}

	//What the model contributed is the difference the load makes to the world's
	//collections. They are flat and shared - every file merges into the same three
	//maps - so this is the only moment the provenance of an asset is knowable, and
	//it is what lets an editor show a model's meshes and animations under it.
	const std::set<std::string> meshes_before = KeySet(meshes);
	const std::set<std::string> materials_before = KeySet(materials);
	const std::set<std::string> animations_before = KeySet(animations);

	std::set<ECS::Entity> entities = LoadFBX(model_file, triangulate, relative, materials, meshes,
		shapes, templates_coordinator, vertex_buffer, use_animation_names);
	//LoadFBX dedups by path and returns nothing at all the second time a file is
	//asked for, so registering that empty result would *unregister* a model the
	//first call had loaded. A game that loads its own asset files after opening a
	//level which already listed them (DemoGame does exactly this for its troll and
	//zombie animation files) must not lose them to the second call.
	if (entities.empty() && IsModelLoaded(name)) {
		return;
	}
	ModelAssets assets;
	assets.file = model_file;
	assets.triangulate = triangulate;
	assets.meshes = NewKeys(meshes_before, meshes);
	assets.materials = NewKeys(materials_before, materials);
	assets.animation_sets = NewKeys(animations_before, animations);
	model_assets[name] = std::move(assets);
	model_entities[name] = std::move(entities);
}

void World::LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names) {
	LoadModel(template_file, triangulate, relative, use_animation_names);
}

bool World::IsModelLoaded(const std::string& name) const {
	return model_entities.find(name) != model_entities.end();
}

std::vector<std::string> World::ListModels() const {
	std::vector<std::string> names;
	names.reserve(model_entities.size());
	for (const auto& [name, entities] : model_entities) {
		names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

const World::ModelAssets* World::GetModelAssets(const std::string& name) const {
	auto it = model_assets.find(name);
	return (it != model_assets.end()) ? &it->second : nullptr;
}

const std::set<ECS::Entity>& World::GetModelEntities(const std::string& name) {
	static const std::set<ECS::Entity> none;
	auto it = model_entities.find(name);
	return (it != model_entities.end()) ? it->second : none;
}

bool World::IsAuthoredTemplate(const std::string& name) const {
	return authored_templates.find(name) != authored_templates.end();
}

const nlohmann::json* World::GetTemplateComponents(const std::string& name) const {
	auto it = authored_templates.find(name);
	return (it != authored_templates.end()) ? &it->second : nullptr;
}

const nlohmann::json* World::GetTemplateParts(const std::string& name) const {
	auto it = template_parts.find(name);
	return (it != template_parts.end()) ? &it->second : nullptr;
}

bool World::IsComposedTemplate(const std::string& name) const {
	const nlohmann::json* parts = GetTemplateParts(name);
	return parts != nullptr && parts->is_array() && !parts->empty();
}

//The template a part entry points at, or "" for a malformed entry. One place, because
//every walk of the part tree below has to agree on what a part references.
static std::string PartTemplate(const nlohmann::json& part) {
	if (!part.is_object() || !part.contains("template") || !part["template"].is_string()) {
		return std::string();
	}
	return part["template"];
}

bool World::CanComposeTemplate(const std::string& name, const std::string& part_template) const {
	if (name.empty() || part_template.empty()) {
		return false;
	}
	if (name == part_template) {
		return false;
	}
	//Would `part_template` reach `name` again through its own parts? Depth-first over
	//the part graph, with a visited set so a cycle that does not involve `name` (which
	//cannot exist, but a hand-edited file can still describe one) terminates too.
	std::set<std::string> visited;
	std::vector<std::string> pending{ part_template };
	while (!pending.empty()) {
		const std::string current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second) {
			continue;
		}
		const nlohmann::json* parts = GetTemplateParts(current);
		if (parts == nullptr || !parts->is_array()) {
			continue;
		}
		for (const auto& part : *parts) {
			const std::string referenced = PartTemplate(part);
			if (referenced.empty()) {
				continue;
			}
			if (referenced == name) {
				return false;
			}
			pending.push_back(referenced);
		}
	}
	return true;
}

//Templates only - models are their own registry (ListModels), and an imported
//asset file has not been a placeable thing since they were separated.
std::vector<std::string> World::ListTemplates() const {
	std::vector<std::string> names;
	names.reserve(template_entities.size());
	for (const auto& [name, entities] : template_entities) {
		names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

//The components a template *entity* carries, in the order they must be applied.
//
//This is exactly the set SpawnInstance reads off the template when it clones one,
//and nothing else: everything a template can hold beyond this is applied to the
//spawned entity instead (see the CreateTemplate contract in World.h).
//
//Order matters twice over. Mesh precedes Bounds because a Bounds block with no
//extents measures itself from the entity's mesh, and Material precedes nothing but
//is kept next to Mesh so an asset swap reads as one step. Base and Transform come
//first because a component may reach for them (nothing in this set does today,
//but Physics-style siblings-lookups are the norm elsewhere).
static const char* TEMPLATE_ENTITY_COMPONENTS[] = {
	Components::Base::NAME,
	Components::Transform::NAME,
	Components::Mesh::NAME,
	Components::Material::NAME,
	Components::Bounds::NAME,
};

bool World::CreateTemplate(const std::string& name, const nlohmann::json& components,
	const nlohmann::json& parts, std::string& error) {
	if (!CreateTemplate(name, components, error)) {
		return false;
	}
	if (parts.is_array() && !parts.empty()) {
		template_parts[name] = parts;
	}
	else {
		template_parts.erase(name);
	}
	return true;
}

bool World::CreateTemplate(const std::string& name, const nlohmann::json& components,
	std::string& error) {
	if (name.empty()) {
		error = "template name is empty";
		return false;
	}
	if (!components.is_object()) {
		error = "template components must be a JSON object";
		return false;
	}
	//An FBX template already owns this registry key, and its entities came out of a
	//file we cannot rebuild - replacing it would leave the level unable to reload.
	if (IsTemplateLoaded(name) && !IsAuthoredTemplate(name)) {
		error = "template name already used by an imported object: " + name;
		return false;
	}

	//Redefining an authored template reuses its entity rather than recreating it:
	//the entity only ever holds the fixed set above, all of which are add-or-update,
	//and keeping it alive means nothing that resolved the template by name (a level
	//record's "template" key) is invalidated by an edit. Looked up through the
	//registry rather than by entity name, because the entity is registered under
	//TEMPLATE_ENTITY_PREFIX + name (see the header for why).
	ECS::Entity e = GetTemplateEntity(name);
	const bool fresh = (e == ECS::INVALID_ENTITY_ID);
	if (fresh) {
		e = templates_coordinator->CreateEntity(TEMPLATE_ENTITY_PREFIX + name);
		if (e == ECS::INVALID_ENTITY_ID) {
			error = "could not create template entity: " + name;
			return false;
		}
		templates_coordinator->AddComponent<Components::Base>(e,
			Components::Base{ .name = name, .id = e, .draw_method = Components::eDrawMethod::DRAW_SCREEN });
		templates_coordinator->AddComponent<Components::Transform>(e, Components::Transform{});
		templates_coordinator->AddComponent<Components::Bounds>(e, Components::Bounds{});
		templates_coordinator->AddComponent<Components::Mesh>(e);
		templates_coordinator->AddComponent<Components::Material>(e);
		templates_coordinator->AddComponent<Components::Lighted>(e);
	}

	//Applied against the templates coordinator, not the scene one: MakeSerializeContext
	//is bound to the scene and would build the template's mesh/material onto whatever
	//scene entity happens to share this id.
	ECS::SerializeContext ctx;
	ctx.world = this;
	ctx.coordinator = templates_coordinator;
	const ECS::ComponentRegistry& registry = ECS::ComponentRegistry::Instance();
	for (const char* component : TEMPLATE_ENTITY_COMPONENTS) {
		const ECS::ComponentDesc* desc = registry.Find(component);
		if (desc == nullptr) {
			continue;
		}
		const bool authored_block = components.contains(component);
		//A template whose bounds are not authored measures them from its mesh, and
		//has to do so again every time the mesh is swapped. Zero extents is the
		//signal Bounds::FromJson takes as "measure me"; without clearing them first,
		//a redefinition would keep the *previous* mesh's box and cull or mis-collide
		//every instance placed afterwards.
		if (!authored_block && std::string(component) == Components::Bounds::NAME) {
			templates_coordinator->GetComponent<Components::Bounds>(e).local_box.Extents =
				{ 0.0f, 0.0f, 0.0f };
		}
		//An absent block is still applied, as an empty one: Mesh and Material turn
		//that into the default cube / white material, which is what makes a template
		//with nothing authored yet immediately placeable.
		desc->apply(ctx, e, authored_block ? components[component] : nlohmann::json::object());
	}

	template_entities[name] = { e };
	authored_templates[name] = components;
	return true;
}

bool World::RemoveTemplate(const std::string& name) {
	if (!IsAuthoredTemplate(name)) {
		//FBX templates are not removable: their entities, meshes and materials came
		//from a file the level still lists, and would come straight back on reload.
		return false;
	}
	for (ECS::Entity e : GetTemplateEntities(name)) {
		templates_coordinator->DestroyEntity(e);
	}
	template_entities.erase(name);
	authored_templates.erase(name);
	template_parts.erase(name);
	return true;
}

ECS::Entity World::GetTemplateEntity(const std::string& name) {
	ECS::Entity fallback = ECS::INVALID_ENTITY_ID;
	for (ECS::Entity e : GetTemplateEntities(name)) {
		if (fallback == ECS::INVALID_ENTITY_ID) {
			fallback = e;
		}
		//The renderable part, i.e. the one SpawnInstance treats as primary: an FBX
		//can register armatures and empties alongside its meshes.
		if (templates_coordinator->ContainsComponent<Components::Mesh>(e) &&
			templates_coordinator->ContainsComponent<Components::Bounds>(e) &&
			templates_coordinator->ContainsComponent<Components::Transform>(e)) {
			return e;
		}
	}
	return fallback;
}

bool World::GetTemplateBaseTransform(const std::string& name, float3& position,
	float4& rotation, float3& scale) {
	position = { 0.0f, 0.0f, 0.0f };
	rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	scale = { 1.0f, 1.0f, 1.0f };
	ECS::Entity te = GetTemplateEntity(name);
	if (te == ECS::INVALID_ENTITY_ID ||
		!templates_coordinator->ContainsComponent<Components::Transform>(te)) {
		return false;
	}
	//The primary part's transform, which is the one SpawnInstance composes into a
	//single-part instance - and into part 0 of a multi-part one.
	const Components::Transform& t =
		templates_coordinator->GetConstComponent<Components::Transform>(te);
	position = t.position;
	rotation = t.rotation;
	scale = t.scale;
	return true;
}

bool World::ReadTemplateFile(const std::string& file, bool relative, std::string& name,
	nlohmann::json& components, nlohmann::json& parts, std::string& error) {
	std::string full_path = file;
	if (relative && file.find(":") == std::string::npos) {
		full_path = path + file;
	}
	nlohmann::json definition;
	try {
		definition = nlohmann::json::parse(std::ifstream(full_path));
	}
	catch (const std::exception& ex) {
		error = std::string("could not read template file ") + full_path + ": " + ex.what();
		return false;
	}
	name = definition.value("name", std::string());
	if (name.empty()) {
		name = std::filesystem::path(full_path).filename().replace_extension().string();
	}
	components = (definition.contains("components") && definition["components"].is_object())
		? definition["components"] : nlohmann::json::object();
	parts = (definition.contains("parts") && definition["parts"].is_array())
		? definition["parts"] : nlohmann::json::array();
	return true;
}

bool World::LoadTemplateFile(const std::string& file, bool relative, std::string& error) {
	std::string name;
	nlohmann::json components;
	nlohmann::json parts;
	if (!ReadTemplateFile(file, relative, name, components, parts, error)) {
		return false;
	}
	return CreateTemplate(name, components, parts, error);
}

bool World::SaveTemplateFile(const std::string& name, const std::string& file, std::string& error) {
	const nlohmann::json* components = GetTemplateComponents(name);
	if (components == nullptr) {
		error = "unknown template: " + name;
		return false;
	}
	nlohmann::json definition;
	definition["name"] = name;
	definition["components"] = *components;
	if (const nlohmann::json* parts = GetTemplateParts(name); parts != nullptr && !parts->empty()) {
		definition["parts"] = *parts;
	}

	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	std::ofstream out(file);
	if (!out.is_open()) {
		error = "could not write " + file;
		return false;
	}
	out << definition.dump(4);
	out.close();
	return true;
}

std::vector<std::string> World::GetMeshAnimations(const std::string& mesh_name) {
	std::vector<std::string> names;
	Core::MeshData* mesh = meshes.Get(mesh_name);
	if (mesh == nullptr) {
		return names;
	}
	//Mesh::SetAnimation looks a name up by scanning every skeleton, every joint and
	//every animation on it, so that is what is enumerated here - anything found this
	//way is guaranteed to be settable.
	std::set<std::string> unique;
	for (const std::shared_ptr<Core::Skeleton>& skeleton : mesh->skeletons) {
		if (skeleton == nullptr) {
			continue;
		}
		for (const Core::JointCpuData& joint : skeleton->CpuData()) {
			for (const Core::JointAnim& animation : joint.animations) {
				if (!animation.name.empty() && !animation.key_frames.empty()) {
					unique.insert(animation.name);
				}
			}
		}
	}
	names.assign(unique.begin(), unique.end());
	return names;
}

std::vector<std::string> World::GetAnimationSetClips(const std::string& set_name) const {
	std::vector<std::string> names;
	const std::shared_ptr<Core::Skeleton>* skl = animations.Get(set_name);
	if (skl == nullptr || *skl == nullptr) {
		return names;
	}
	//Same walk as GetMeshAnimations, over one set: a clip is an animation some joint
	//of the skeleton carries key frames for, and the joints do not all carry all of
	//them - so the union over joints is the set's clip list.
	std::set<std::string> unique;
	for (const Core::JointCpuData& joint : (*skl)->CpuData()) {
		for (const Core::JointAnim& animation : joint.animations) {
			if (!animation.name.empty() && !animation.key_frames.empty()) {
				unique.insert(animation.name);
			}
		}
	}
	names.assign(unique.begin(), unique.end());
	return names;
}

std::string World::FindAnimationSet(const std::string& clip) const {
	if (clip.empty()) {
		return {};
	}
	for (const std::string& set_name : animations.Keys()) {
		for (const std::string& name : GetAnimationSetClips(set_name)) {
			if (name == clip) {
				return set_name;
			}
		}
	}
	return {};
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
	mesh_buffers_dirty = false;
}

bool World::SetMeshSmooth(Core::MeshData* mesh, bool smooth) {
	if (mesh == nullptr || !mesh->SetSmooth(smooth)) {
		return false;
	}
	//A generated level of detail is a copy of this mesh's surface and has to shade
	//like it. Left behind, a model would change appearance the moment it dropped a
	//level - which reads as the LOD being a different model, not as a shading flag
	//that only reached half of it.
	for (const GeneratedMesh& generated : generated_meshes) {
		if (generated.source != mesh->name) {
			continue;
		}
		if (Core::MeshData* lod = meshes.Get(generated.name)) {
			lod->SetSmooth(smooth);
		}
	}
	mesh_buffers_dirty = true;
	return true;
}

bool World::SetMeshLods(Core::MeshData* mesh, const std::vector<std::string>& names,
	const std::vector<float>& distances) {
	if (mesh == nullptr) {
		return false;
	}
	bool resolved_all = true;
	std::vector<Core::MeshData*> chain;
	std::vector<float> chain_distances;
	for (size_t i = 0; i < names.size(); ++i) {
		Core::MeshData* lod = meshes.Get(names[i]);
		if (lod == nullptr) {
			printf("World::SetMeshLods: unknown mesh '%s'.\n", names[i].c_str());
			resolved_all = false;
			continue;
		}
		chain.push_back(lod);
		//Kept in step with `chain` rather than passed through whole: a name that did
		//not resolve must not shift every distance after it onto the wrong level.
		chain_distances.push_back(i < distances.size() ? distances[i] : 0.0f);
	}
	//Nothing is uploaded and nothing is rebuilt - the alternates are meshes the world
	//already holds, already in the vertex buffer. This only records which of them
	//stand in for which, so it needs no FlushMeshBuffers.
	return mesh->SetLods(chain, chain_distances) && resolved_all;
}

//== Generated meshes ==========================================================
//
//A cached simplification, on disk. It is a cache and never the source of truth:
//the level records the name/source/ratio recipe that produced it, so every field
//written here that describes the input exists to answer one question - is this
//still the mesh that recipe would produce today? Anything that says no (a changed
//model, a rebuilt engine whose Vertex grew a field, a file from a future version)
//throws the file away and simplifies again, which costs a second and cannot be
//wrong. Trusting a stale file instead would draw yesterday's model as today's LOD,
//and would do it silently.
namespace {
	constexpr char LOD_CACHE_MAGIC[8] = { 'H','B','L','O','D','M','S','H' };
	constexpr uint32_t LOD_CACHE_VERSION = 1;

	struct LodGeometry {
		std::vector<Core::Vertex> vertices;
		std::vector<uint32_t> indices;
		//Empty when the source carried no grouping, which pins the generated mesh
		//to the shading it was built with exactly as it pins the source.
		std::vector<uint32_t> smooth_groups;
	};

	template<class T>
	void WritePod(std::ofstream& os, const T& value) {
		os.write(reinterpret_cast<const char*>(&value), sizeof(T));
	}

	template<class T>
	bool ReadPod(std::ifstream& is, T& value) {
		is.read(reinterpret_cast<char*>(&value), sizeof(T));
		return (bool)is;
	}

	bool WriteLodCache(const std::string& file, const std::string& source_name,
		uint32_t source_vertices, uint32_t source_indices, float ratio,
		const LodGeometry& geometry) {
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		std::ofstream os(file, std::ios::binary | std::ios::trunc);
		if (!os) {
			return false;
		}
		os.write(LOD_CACHE_MAGIC, sizeof(LOD_CACHE_MAGIC));
		WritePod(os, LOD_CACHE_VERSION);
		WritePod(os, (uint32_t)sizeof(Core::Vertex));
		WritePod(os, source_vertices);
		WritePod(os, source_indices);
		WritePod(os, ratio);
		WritePod(os, (uint32_t)geometry.vertices.size());
		WritePod(os, (uint32_t)geometry.indices.size());
		WritePod(os, (uint32_t)geometry.smooth_groups.size());
		const uint32_t name_length = (uint32_t)source_name.size();
		WritePod(os, name_length);
		os.write(source_name.data(), name_length);
		os.write(reinterpret_cast<const char*>(geometry.vertices.data()),
			(std::streamsize)(geometry.vertices.size() * sizeof(Core::Vertex)));
		os.write(reinterpret_cast<const char*>(geometry.indices.data()),
			(std::streamsize)(geometry.indices.size() * sizeof(uint32_t)));
		os.write(reinterpret_cast<const char*>(geometry.smooth_groups.data()),
			(std::streamsize)(geometry.smooth_groups.size() * sizeof(uint32_t)));
		return (bool)os;
	}

	bool ReadLodCache(const std::string& file, const std::string& source_name,
		uint32_t source_vertices, uint32_t source_indices, float ratio,
		LodGeometry& geometry) {
		std::ifstream is(file, std::ios::binary);
		if (!is) {
			return false;
		}
		char magic[sizeof(LOD_CACHE_MAGIC)] = {};
		is.read(magic, sizeof(magic));
		if (!is || memcmp(magic, LOD_CACHE_MAGIC, sizeof(magic)) != 0) {
			return false;
		}
		uint32_t version = 0, stride = 0, cached_source_vertices = 0, cached_source_indices = 0;
		uint32_t vertex_count = 0, index_count = 0, group_count = 0, name_length = 0;
		float cached_ratio = 0.0f;
		if (!ReadPod(is, version) || !ReadPod(is, stride) ||
			!ReadPod(is, cached_source_vertices) || !ReadPod(is, cached_source_indices) ||
			!ReadPod(is, cached_ratio) || !ReadPod(is, vertex_count) ||
			!ReadPod(is, index_count) || !ReadPod(is, group_count) ||
			!ReadPod(is, name_length)) {
			return false;
		}
		if (version != LOD_CACHE_VERSION || stride != (uint32_t)sizeof(Core::Vertex)) {
			return false;
		}
		std::string cached_source(name_length, '\0');
		is.read(cached_source.data(), name_length);
		if (!is || cached_source != source_name) {
			return false;
		}
		//The source is identified by what it *is*, not by when it was written: a
		//timestamp says nothing useful about an .fbx that was re-exported unchanged,
		//and nothing at all about one edited in place by a tool that preserves it.
		if (cached_source_vertices != source_vertices || cached_source_indices != source_indices ||
			fabsf(cached_ratio - ratio) > 1e-4f) {
			return false;
		}
		if (vertex_count == 0 || index_count == 0 ||
			(group_count != 0 && group_count != vertex_count)) {
			return false;
		}
		geometry.vertices.resize(vertex_count);
		geometry.indices.resize(index_count);
		geometry.smooth_groups.resize(group_count);
		is.read(reinterpret_cast<char*>(geometry.vertices.data()),
			(std::streamsize)(vertex_count * sizeof(Core::Vertex)));
		is.read(reinterpret_cast<char*>(geometry.indices.data()),
			(std::streamsize)(index_count * sizeof(uint32_t)));
		if (group_count != 0) {
			is.read(reinterpret_cast<char*>(geometry.smooth_groups.data()),
				(std::streamsize)(group_count * sizeof(uint32_t)));
		}
		if (!is) {
			geometry = LodGeometry{};
			return false;
		}
		//A truncated file that happens to end on a record boundary would pass every
		//check above, so the indices are what is actually going to be drawn with.
		for (uint32_t index : geometry.indices) {
			if (index >= vertex_count) {
				geometry = LodGeometry{};
				return false;
			}
		}
		return true;
	}

	//A mesh is named after the node it came out of an .fbx as, and those names are
	//not file names: Sponza's are "sponza:sponza_381". Left alone that colon does
	//not fail - NTFS reads it as an alternate data stream, so the geometry is
	//written *into* a zero-length file called "sponza" and silently lost, and the
	//next load finds no cache and rebuilds every time. Only the file is spelled
	//differently; the asset keeps the name everything else refers to it by.
	std::string SafeFileStem(const std::string& name) {
		std::string stem;
		stem.reserve(name.size());
		for (char c : name) {
			const bool illegal = (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
				c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 0x20);
			stem.push_back(illegal ? '_' : c);
		}
		if (stem.empty()) {
			stem = "mesh";
		}
		return stem;
	}

	bool BuildLodGeometry(const Core::MeshData& source, float ratio, LodGeometry& geometry) {
		Core::SimplifyResult result;
		if (!Core::SimplifyMesh(source.vertices, source.indices, ratio, result)) {
			return false;
		}
		geometry.vertices = std::move(result.vertices);
		geometry.indices = std::move(result.indices);
		//Carry the shading data across, so the generated mesh can be smoothed and
		//unsmoothed with the mesh it stands in for instead of being frozen at
		//whatever that mesh was showing the moment it was generated - which would
		//show up as the model changing its shading when it drops a level.
		if (source.flat_frames.size() != source.vertices.size() ||
			source.smooth_groups.size() != source.vertices.size()) {
			return !geometry.indices.empty();
		}
		geometry.smooth_groups.resize(geometry.vertices.size());
		std::unordered_map<uint32_t, uint32_t> first_of_group;
		for (size_t i = 0; i < geometry.vertices.size(); ++i) {
			const uint32_t src = result.source_vertex[i];
			//MeshData::Init reads the frames it is handed as the *flat* ones, so what
			//goes in here has to be the unfused frame - the source's live vertices
			//carry whatever its current `smooth` produced, and handing those over
			//would fuse an already fused frame.
			geometry.vertices[i].Normal = source.flat_frames[src].normal;
			geometry.vertices[i].Tangent = source.flat_frames[src].tangent;
			geometry.vertices[i].Bitangent = source.flat_frames[src].bitangent;
			//The clones of one control point that survived the reduction are still
			//clones of each other, and the first of them stands for the group exactly
			//as the original control point did.
			const uint32_t group = source.smooth_groups[src];
			auto it = first_of_group.find(group);
			if (it == first_of_group.end()) {
				first_of_group.emplace(group, (uint32_t)i);
				geometry.smooth_groups[i] = (uint32_t)i;
			}
			else {
				geometry.smooth_groups[i] = it->second;
			}
		}
		return !geometry.indices.empty();
	}
}

Core::MeshData* World::InstallGeneratedMesh(const std::string& name,
	const std::vector<Core::Vertex>& vertices, const std::vector<uint32_t>& indices,
	const std::vector<uint32_t>& smooth_groups, std::shared_ptr<Core::Skeleton> skeleton,
	bool smooth) {
	//Insert before Init, like every other mesh here: MeshData refuses to be copied
	//once initialized and Insert copies into the collection.
	meshes.Insert(name, Core::MeshData{});
	Core::MeshData* mesh = meshes.Get(name);
	if (mesh == nullptr) {
		return nullptr;
	}
	mesh->Init(vertex_buffer, name, vertices, indices, skeleton,
		smooth_groups.empty() ? nullptr : &smooth_groups, smooth);
	//The world vertex buffer is immutable and holds every mesh, so the geometry is
	//in the CPU copy alone until the one rebuild happens - FlushMeshBuffers, between
	//frames. Before Init() there is nothing to do: it uploads the buffers for the
	//first time and picks this up with everything else.
	mesh_buffers_dirty = true;
	return mesh;
}

bool World::GenerateMeshLod(const std::string& source_mesh, float ratio,
	std::string& out_name, std::string& error) {
	out_name.clear();
	if (vertex_buffer == nullptr) {
		error = "no vertex buffer";
		return false;
	}
	if (!(ratio > 0.0f) || !(ratio < 1.0f)) {
		error = "ratio must be between 0 and 1";
		return false;
	}
	Core::MeshData* source = meshes.Get(source_mesh);
	if (source == nullptr) {
		error = "unknown mesh '" + source_mesh + "'";
		return false;
	}
	//Read off the source before anything is inserted into the collection it lives in.
	std::shared_ptr<Core::Skeleton> skeleton =
		source->skeletons.empty() ? nullptr : source->skeletons[0];
	const bool smooth = source->smooth;
	const uint32_t source_vertices = source->vertexCount;
	const uint32_t source_indices = source->indexCount;

	std::string name;
	for (int i = 1; i < 1000; ++i) {
		const std::string candidate = source_mesh + "_lod" + std::to_string(i);
		if (meshes.Get(candidate) == nullptr) {
			name = candidate;
			break;
		}
	}
	if (name.empty()) {
		error = "no free name for a level of '" + source_mesh + "'";
		return false;
	}

	LodGeometry geometry;
	if (!BuildLodGeometry(*source, ratio, geometry)) {
		error = "'" + source_mesh + "' cannot be simplified to " +
			std::to_string((int)(ratio * 100.0f)) + "% (see the log)";
		return false;
	}
	//Two mesh names that differ only in a character the sanitizer had to replace
	//would be cached into one file, and each load would find the other one's
	//geometry there and rebuild. Rare, silent, and cheap to rule out.
	const std::string stem = SafeFileStem(name);
	std::string file;
	for (int attempt = 0; ; ++attempt) {
		file = std::string(GENERATED_MESH_DIR) + "\\" + stem +
			(attempt == 0 ? "" : "_" + std::to_string(attempt)) + ".hbmesh";
		bool taken = false;
		for (const GeneratedMesh& other : generated_meshes) {
			if (other.file == file) {
				taken = true;
				break;
			}
		}
		if (!taken) {
			break;
		}
	}
	if (!WriteLodCache(path + file, source_mesh, source_vertices, source_indices, ratio,
		geometry)) {
		//Not fatal. The mesh is in the world either way; what is lost is the saving
		//on the next load, which will simplify it again from the recipe.
		LOG_WARN("World::GenerateMeshLod: could not write '%s'", (path + file).c_str());
	}
	if (InstallGeneratedMesh(name, geometry.vertices, geometry.indices,
		geometry.smooth_groups, skeleton, smooth) == nullptr) {
		error = "could not register the generated mesh";
		return false;
	}
	generated_meshes.push_back(GeneratedMesh{ name, source_mesh, ratio, file });
	LOG_INFO("World::GenerateMeshLod: '%s' from '%s' at %.0f%% - %zu vertices of %u, %zu indices",
		name.c_str(), source_mesh.c_str(), ratio * 100.0f, geometry.vertices.size(),
		source_vertices, geometry.indices.size());
	out_name = name;
	return true;
}

void World::LoadGeneratedMeshes(const nlohmann::json& entries) {
	for (const json& entry : entries) {
		const std::string name = entry.value("name", std::string());
		const std::string source_name = entry.value("source", std::string());
		const float ratio = entry.value("ratio", 0.5f);
		if (name.empty() || source_name.empty()) {
			printf("World::Load: generated mesh entry without a \"name\" or \"source\", skipping.\n");
			continue;
		}
		if (meshes.Get(name) != nullptr) {
			//A model already registered a mesh of that name. Overwriting it would
			//swap a real asset for a stand-in behind the back of everything drawing
			//it, so the generated one is the one that gives way.
			printf("World::Load: generated mesh '%s' is already a mesh, skipping.\n", name.c_str());
			continue;
		}
		Core::MeshData* source = meshes.Get(source_name);
		if (source == nullptr) {
			printf("World::Load: generated mesh '%s' comes from unknown mesh '%s', skipping.\n",
				name.c_str(), source_name.c_str());
			continue;
		}
		const std::string file = entry.value("file",
			std::string(GENERATED_MESH_DIR) + "\\" + SafeFileStem(name) + ".hbmesh");
		std::shared_ptr<Core::Skeleton> skeleton =
			source->skeletons.empty() ? nullptr : source->skeletons[0];
		const bool smooth = source->smooth;
		const uint32_t source_vertices = source->vertexCount;
		const uint32_t source_indices = source->indexCount;

		LodGeometry geometry;
		const bool cached = ReadLodCache(path + file, source_name, source_vertices,
			source_indices, ratio, geometry);
		if (!cached) {
			if (!BuildLodGeometry(*source, ratio, geometry)) {
				printf("World::Load: generated mesh '%s' could not be rebuilt from '%s', skipping.\n",
					name.c_str(), source_name.c_str());
				continue;
			}
			//Rewritten so the next load is a read again. This is the path a changed
			//model takes, and it is why the cache never has to be invalidated by hand.
			if (!WriteLodCache(path + file, source_name, source_vertices, source_indices,
				ratio, geometry)) {
				LOG_WARN("World::LoadGeneratedMeshes: could not write '%s'", (path + file).c_str());
			}
		}
		if (InstallGeneratedMesh(name, geometry.vertices, geometry.indices,
		geometry.smooth_groups, skeleton, smooth) == nullptr) {
			continue;
		}
		generated_meshes.push_back(GeneratedMesh{ name, source_name, ratio, file });
		LOG_INFO("World::LoadGeneratedMeshes: '%s' from '%s' (%s) - %zu vertices",
			name.c_str(), source_name.c_str(), cached ? "cached" : "rebuilt",
			geometry.vertices.size());
	}
}

void World::FlushMeshBuffers() {
	if (!mesh_buffers_dirty) {
		return;
	}
	if (scene_init) {
		RefreshMeshBuffers();
	}
	mesh_buffers_dirty = false;
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

//A part entry's own transform, defaulted so a part that only names a template lands on
//its root.
static void ReadPartTransform(const nlohmann::json& part, float3& position, float4& rotation,
	float3& scale) {
	position = { 0.0f, 0.0f, 0.0f };
	rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	scale = { 1.0f, 1.0f, 1.0f };
	if (part.contains("position")) {
		const auto& p = part["position"];
		position = { p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f) };
	}
	if (part.contains("rotation")) {
		const auto& r = part["rotation"];
		rotation = { r.value("x", 0.0f), r.value("y", 0.0f), r.value("z", 0.0f), r.value("w", 1.0f) };
	}
	if (part.contains("scale")) {
		const auto& s = part["scale"];
		scale = { s.value("x", 1.0f), s.value("y", 1.0f), s.value("z", 1.0f) };
	}
}

//How far a spawn may descend through parts of parts. The cycle guard already refuses a
//template that reaches itself; this catches a chain that is merely absurd before it
//spawns thousands of entities.
static constexpr int MAX_COMPOSED_DEPTH = 8;

bool World::ComposedWorldPose(ECS::Entity e, float3& position, float4& rotation) const {
	if (!coordinator->ContainsComponent<Components::Transform>(e)) {
		return false;
	}
	const Components::Transform& t = coordinator->GetConstComponent<Components::Transform>(e);
	position = t.position;
	rotation = t.rotation;
	if (!coordinator->ContainsComponent<Components::Base>(e)) {
		return false;
	}
	//Only the position/rotation chain, because that is the only kind of parent a body
	//can hang off: a bone-attached part never has one (its pose changes every frame, so
	//a collider made from it would be stale immediately), which is what makes walking
	//parents here equivalent to what StaticMeshSystem computes.
	bool composed = false;
	ECS::Entity parent = coordinator->GetConstComponent<Components::Base>(e).parent;
	for (int depth = 0; parent != ECS::INVALID_ENTITY_ID && depth < MAX_COMPOSED_DEPTH; ++depth) {
		if (!coordinator->ContainsComponent<Components::Transform>(parent) ||
			!coordinator->ContainsComponent<Components::Base>(parent)) {
			break;
		}
		const Components::Base& parent_base = coordinator->GetConstComponent<Components::Base>(parent);
		const Components::Transform& pt = coordinator->GetConstComponent<Components::Transform>(parent);
		vector3d offset = DirectX::XMVector3Transform(
			DirectX::XMVectorSet(position.x, position.y, position.z, 1.0f),
			DirectX::XMMatrixRotationQuaternion(XMLoadFloat4(&pt.rotation)));
		DirectX::XMStoreFloat3(&position, offset);
		position = ADD_F3_F3(pt.position, position);
		rotation = quaternion_multiply(pt.rotation, rotation);
		composed = true;
		parent = parent_base.parent;
	}
	return composed;
}

void World::CollectInstanceNames(const std::string& instance_name, const std::string& template_name,
	std::set<std::string>& chain, int depth, std::vector<std::string>& out)
{
	//Mirrors SpawnTemplateEntities' naming: a multi-part FBX template suffixes every
	//node with its index (including the non-renderable ones it skips, so the indices of
	//the entities that do exist never shift), a single-part one uses the name as given.
	const size_t nodes = GetTemplateEntities(template_name).size();
	if (nodes > 1) {
		for (size_t i = 0; i < nodes; ++i) {
			out.push_back(instance_name + "_" + std::to_string(i));
		}
	}
	else {
		out.push_back(instance_name);
	}
	const nlohmann::json* parts = GetTemplateParts(template_name);
	if (parts == nullptr || !parts->is_array() || depth >= MAX_COMPOSED_DEPTH) {
		return;
	}
	chain.insert(template_name);
	for (const auto& part : *parts) {
		const std::string part_name = part.value("name", std::string());
		const std::string part_template = PartTemplate(part);
		if (part_name.empty() || part_template.empty() || chain.count(part_template) != 0) {
			continue;
		}
		CollectInstanceNames(instance_name + PART_NAME_SEPARATOR + part_name, part_template,
			chain, depth + 1, out);
	}
	chain.erase(template_name);
}

std::vector<std::string> World::InstanceEntityNames(const std::string& instance_name,
	const std::string& template_name)
{
	std::vector<std::string> names;
	std::set<std::string> chain;
	CollectInstanceNames(instance_name, template_name, chain, 0, names);
	return names;
}

ECS::Entity World::SpawnInstance(const std::string& name, const std::string& template_name,
	const float3& position, const float4& rotation, const float3& scale,
	const std::string& material_name, const nlohmann::json* physics_json,
	std::vector<ECS::Entity>* out_parts)
{
	std::set<std::string> chain;
	return SpawnComposed(name, template_name, position, rotation, scale, material_name,
		physics_json, out_parts, chain, 0);
}

ECS::Entity World::SpawnComposed(const std::string& name, const std::string& template_name,
	const float3& position, const float4& rotation, const float3& scale,
	const std::string& material_name, const nlohmann::json* physics_json,
	std::vector<ECS::Entity>* out_parts, std::set<std::string>& chain, int depth)
{
	ECS::Entity primary = SpawnTemplateEntities(name, template_name, position, rotation, scale,
		material_name, physics_json, out_parts);
	const nlohmann::json* parts = GetTemplateParts(template_name);
	if (primary == ECS::INVALID_ENTITY_ID || parts == nullptr || !parts->is_array()) {
		return primary;
	}
	if (depth >= MAX_COMPOSED_DEPTH) {
		printf("World::SpawnInstance: '%s' nests parts more than %d deep, stopping.\n",
			template_name.c_str(), MAX_COMPOSED_DEPTH);
		return primary;
	}
	chain.insert(template_name);

	//Parts are placed in the root *entity's* frame: its rotation turns their offsets and
	//its position carries them, which is exactly what Base::parent composition does - so
	//an attached part and a detached one at the same offset land in the same place, and
	//flipping "attach" never moves anything. The root's own scale is deliberately not
	//applied: a part is a whole object with a scale of its own, and the instance's scale
	//is what makes the assembly bigger.
	const Components::Transform& root = coordinator->GetConstComponent<Components::Transform>(primary);
	const matrix root_rotation = DirectX::XMMatrixRotationQuaternion(XMLoadFloat4(&root.rotation));

	for (const auto& part : *parts) {
		const std::string part_name = part.value("name", std::string());
		const std::string part_template = PartTemplate(part);
		if (part_name.empty() || part_template.empty()) {
			printf("World::SpawnInstance: part of '%s' without a name or template, skipping.\n",
				template_name.c_str());
			continue;
		}
		if (chain.count(part_template) != 0) {
			printf("World::SpawnInstance: part '%s' of '%s' would compose '%s' into itself, skipping.\n",
				part_name.c_str(), template_name.c_str(), part_template.c_str());
			continue;
		}
		const bool attach = part.value("attach", true);
		const std::string bone = part.value("bone", std::string());

		float3 part_position{};
		float4 part_rotation{};
		float3 part_scale{};
		ReadPartTransform(part, part_position, part_rotation, part_scale);

		float3 spawn_position = part_position;
		float4 spawn_rotation = part_rotation;
		float3 spawn_scale = part_scale;
		if (!bone.empty() && attach) {
			//A socket offset is expressed in the parent mesh's own space, and the whole
			//chain up to the world - the parent's scale included - is applied by
			//StaticMeshSystem through the parent's world matrix. Composing anything in
			//here would apply it twice.
		}
		else {
			spawn_position = MULT_F3_F3(part_position, scale);
			spawn_scale = MULT_F3_F3(part_scale, scale);
			if (!attach) {
				//Detached: there is no parent to compose anything later, so the root's
				//pose is baked in now.
				vector3d offset = DirectX::XMVector3Transform(
					DirectX::XMVectorSet(spawn_position.x, spawn_position.y, spawn_position.z, 1.0f),
					root_rotation);
				float3 rotated{};
				DirectX::XMStoreFloat3(&rotated, offset);
				spawn_position = ADD_F3_F3(root.position, rotated);
				spawn_rotation = quaternion_multiply(root.rotation, part_rotation);
			}
		}

		//A rigid body on an attached part is either stale or actively wrong - see the
		//composed-template block in World.h.
		const bool suppress_physics = attach;

		std::vector<ECS::Entity> part_entities;
		ECS::Entity part_primary = SpawnComposed(name + PART_NAME_SEPARATOR + part_name,
			part_template, spawn_position, spawn_rotation, spawn_scale, std::string(), nullptr,
			&part_entities, chain, depth + 1);
		if (part_primary == ECS::INVALID_ENTITY_ID) {
			continue;
		}
		if (suppress_physics) {
			nlohmann::json strip;
			strip["remove"] = nlohmann::json::array({ Components::Physics::NAME });
			for (ECS::Entity e : part_entities) {
				bool remove = !bone.empty();
				if (!remove && coordinator->ContainsComponent<Components::Physics>(e)) {
					//A static body is kept and seated at the composed pose; a simulated one
					//would drive the Transform and silently undo the attachment.
					remove = coordinator->GetConstComponent<Components::Physics>(e).type !=
						reactphysics3d::BodyType::STATIC;
				}
				if (remove) {
					//Applied even when there is no Physics component to take away: what this
					//records is the *removal*, which is what stops World::Init handing a
					//default static body to a part whose pose changes every frame. Without
					//it a bone-riding part loads with a collider at whatever the joint
					//offset points to from the world origin.
					ApplyComponents(e, strip);
				}
			}
		}
		if (attach) {
			//Only the part's own primary is parented: the rest of a multi-part part are
			//siblings of it in the same spawn, already placed relative to the same pose.
			Components::Base& base = coordinator->GetComponent<Components::Base>(part_primary);
			base.parent = primary;
			base.parent_position = true;
			base.parent_rotation = true;
			base.parent_bone = bone;
			base.parent_joint = Components::Base::UNRESOLVED_JOINT;
			coordinator->GetComponent<Components::Transform>(part_primary).dirty = true;
			//A STATIC body that survived the strip above was built from the Transform,
			//which is now an offset from the root rather than a place in the world.
			if (coordinator->ContainsComponent<Components::Physics>(part_primary)) {
				Components::Physics& p = coordinator->GetComponent<Components::Physics>(part_primary);
				float3 world_position{};
				float4 world_rotation{};
				if (p.body != nullptr && ComposedWorldPose(part_primary, world_position, world_rotation)) {
					std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
					reactphysics3d::Transform bt(
						{ world_position.x, world_position.y, world_position.z },
						{ world_rotation.x, world_rotation.y, world_rotation.z, world_rotation.w });
					p.body->setTransform(bt);
					p.last_body_transform = bt;
				}
			}
		}
		if (part.contains("components") && part["components"].is_object()) {
			nlohmann::json record;
			record["components"] = part["components"];
			for (ECS::Entity e : part_entities) {
				ApplyComponents(e, record);
			}
		}
		if (out_parts != nullptr) {
			out_parts->insert(out_parts->end(), part_entities.begin(), part_entities.end());
		}
	}
	chain.erase(template_name);
	return primary;
}

ECS::Entity World::SpawnTemplateEntities(const std::string& name, const std::string& template_name,
	const float3& position, const float4& rotation, const float3& scale,
	const std::string& material_name, const nlohmann::json* physics_json,
	std::vector<ECS::Entity>* out_parts)
{
	ECS::Entity primary = ECS::INVALID_ENTITY_ID;
	const std::set<ECS::Entity>& parts = GetTemplateEntities(template_name);
	if (parts.empty()) {
		//Reported rather than asserted: a level can name a template whose file has
		//gone missing, and losing one instance is a far better outcome than aborting
		//the whole load (in the editor, than taking the editor down with it).
		printf("World::SpawnInstance: unknown template '%s', instance '%s' not spawned.\n",
			template_name.c_str(), name.c_str());
		return ECS::INVALID_ENTITY_ID;
	}

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
			//The template's LOCAL box: Init scales it by t.scale itself (which already
			//carries the template's own scale, composed above).
			p.Init(phys_world, p.type, nullptr, tbounds.local_box, t.position, t.scale, t.rotation, p.shape);
		}

		coordinator->NotifySignatureChange(e);

		//An authored template carries its own component set (see CreateTemplate):
		//apply it so the instance starts out as the template describes it, rather
		//than as the fixed mesh/material clone above. This is what gives an instance
		//the template's Physics, its selected animation, and any component the engine
		//itself knows nothing about.
		//
		//Transform is excluded deliberately: the instance's pose was just composed
		//from the template's base transform and the spawn transform, and re-applying
		//the template's own would throw that composition away and stack every
		//instance at the same spot.
		auto authored = authored_templates.find(template_name);
		if (authored != authored_templates.end()) {
			nlohmann::json record;
			record["components"] = authored->second;
			record["components"].erase(Components::Transform::NAME);
			ApplyComponents(e, record);
		}

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
			np.Init(phys_world, np.type, shape, bounds.local_box, t.position, t.scale, t.rotation, np.shape);
		}
	}

	coordinator->NotifySignatureChange(e);
	return e;
}

ECS::Entity World::CreateEmptyEntity(const std::string& name, const float3& position,
	const float4& rotation, const float3& scale)
{
	if (name.empty()) {
		printf("World::CreateEmptyEntity: empty entity name.\n");
		return ECS::INVALID_ENTITY_ID;
	}
	if (coordinator->GetEntityByName(name) != ECS::INVALID_ENTITY_ID) {
		printf("World::CreateEmptyEntity: entity name already exists: %s\n", name.c_str());
		return ECS::INVALID_ENTITY_ID;
	}
	ECS::Entity e = coordinator->CreateEntity(name);

	//Base's own defaults are what an entity with nothing on it should have; only
	//its identity has to be filled in (creation_time defaults to now on its own).
	Components::Base base;
	base.name = name;
	base.id = e;
	coordinator->AddComponent<Components::Base>(e, base);

	Components::Transform t;
	t.position = position;
	t.rotation = rotation;
	t.scale = scale;
	t.dirty = true;
	coordinator->AddComponent<Components::Transform>(e, t);

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
		//The record's own block belongs to the object it names, not to the other objects
		//composed into it: a "Physics" override written for the troll must not also land
		//on the sword it carries. A composed part is addressed by its own name instead,
		//under "parts".
		std::set<std::string> root_names;
		const size_t nodes = GetTemplateEntities(template_name).size();
		if (nodes > 1) {
			for (size_t i = 0; i < nodes; ++i) {
				root_names.insert(name + "_" + std::to_string(i));
			}
		}
		else {
			root_names.insert(name);
		}
		for (ECS::Entity part : parts) {
			if (root_names.count(coordinator->GetConstComponent<Components::Base>(part).name) != 0) {
				ApplyComponents(part, instance);
			}
		}
		if (instance.contains("parts") && instance["parts"].is_object()) {
			for (const auto& [part_name, part_entry] : instance["parts"].items()) {
				const std::string entity_name = name + World::PART_NAME_SEPARATOR + part_name;
				ECS::Entity e = coordinator->GetEntityByName(entity_name);
				if (e == ECS::INVALID_ENTITY_ID) {
					printf("World::LoadInstances: instance '%s' overrides unknown part '%s'.\n",
						name.c_str(), part_name.c_str());
					continue;
				}
				ApplyComponents(e, part_entry);
			}
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

		//Load models: the asset files this level draws on ({"file": "....fbx"}),
		//bringing in meshes, materials, collision shapes and animation clips. They
		//come first because everything below names the assets they carry.
		if (jw.contains("models")) {
			for (json& m : jw["models"]) {
				if (!m.contains("file") || !m["file"].is_string()) {
					printf("World::Load: model entry without a \"file\", skipping.\n");
					continue;
				}
				LoadModel(m["file"], m.value("triangulate", false), true);
			}
		}

		//Load templates.
		//
		//Two forms share this list, and everything downstream - instances, a
		//component's "template" key, editor placement - is indifferent to which one a
		//template came from:
		//
		//  {"file": "....tpl"}                - a template kept in its own file,
		//                                       shared between levels.
		//  {"name": ..., "components": {...}} - the same definition written inline,
		//                                       for a template belonging to this level
		//                                       alone. Both forms are equally valid;
		//                                       the editor can move a template between
		//                                       them.
		//
		//A third form is accepted and no longer written: {"file": "....fbx"}, from
		//before models and templates were separate registries. It loads as a model,
		//which is what it always really was, and a level that places instances
		//straight off it keeps working through GetTemplateEntities' model fallback.
		//Saving from the editor moves the entry into "models".
		//
		//Templates are *deferred* to just below the materials and meshes sections:
		//their component blocks name materials and animation clips by name, and those
		//are only complete once those sections have run. Creating them here would
		//resolve every material to the default white one instead - silently, and for
		//every instance of the template.
		//name, components, parts - see CreateTemplate and the composed-template block in
		//World.h for the third.
		std::vector<std::tuple<std::string, json, json>> authored_templates_to_create;
		if (jw.contains("templates")) {
			auto& template_files = jw["templates"];
			for (json& t : template_files) {
				if (t.contains("components") && t["components"].is_object()) {
					const std::string name = t.value("name", std::string());
					if (name.empty()) {
						printf("World::Load: inline template without a \"name\", skipping.\n");
						continue;
					}
					authored_templates_to_create.push_back({ name, t["components"],
						(t.contains("parts") && t["parts"].is_array()) ? t["parts"] : json::array() });
					continue;
				}
				if (!t.contains("file") || !t["file"].is_string()) {
					printf("World::Load: template entry with neither \"file\" nor \"components\", skipping.\n");
					continue;
				}
				const std::string file = t["file"];
				if (std::filesystem::path(file).extension() == ".tpl") {
					std::string name;
					json components;
					json parts;
					std::string error;
					if (ReadTemplateFile(file, true, name, components, parts, error)) {
						authored_templates_to_create.push_back({ name, components, parts });
					}
					else {
						//A missing or malformed template must not abort the level: the
						//instances referencing it are skipped by SpawnInstance's own
						//guard, and everything else in the scene still loads.
						printf("World::Load: %s\n", error.c_str());
					}
				}
				else {
					LoadModel(file, t.value("triangulate", false), true);
				}
			}
			if (OnLoadProgress != nullptr) { OnLoadProgress(*progress += 10.0f * progress_unit); }
		}

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
		//Meshes this level had generated rather than imported - a level of detail
		//simplified out of a model instead of exported beside it (GenerateMeshLod).
		//Here because they are made *from* the meshes above and named *by* the
		//templates below: a template whose Mesh block lists one in its LOD chain would
		//otherwise have that level silently dropped as an unknown mesh.
		if (jw.contains("generated_meshes") && jw["generated_meshes"].is_array()) {
			LoadGeneratedMeshes(jw["generated_meshes"]);
		}

		//Authored templates, now that the materials and animation sets their component
		//blocks name by are all in place (see the templates phase above for why this is
		//not done up there).
		for (auto& [name, components, parts] : authored_templates_to_create) {
			std::string error;
			if (!CreateTemplate(name, components, parts, error)) {
				printf("World::Load: %s\n", error.c_str());
			}
		}
		if (jw.contains("templates")) {
			coordinator->SendEvent(this, EVENT_ID_TEMPLATES_LOADED);
		}

		//Load editor-placed object instances (entities cloned from templates at load
		//time, as opposed to "entities" below which only modifies already-existing
		//named entities). After the templates phase completes, and after materials, so
		//an instance's own "material" override resolves too.
		if (jw.contains("instances")) {
			LoadInstances(jw["instances"]);
		}

		//Load sky
		if (jw.contains("sky")) {
			LoadSky(jw["sky"]);
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
					Components::DirectionalLight::CascadeSettings cascades;
					cascades.FromJson(light);
					directional.Init(ColorRGBFromStr(light["color"]), float3{ light["direction"]["x"], light["direction"]["y"], light["direction"]["z"] }, light["cast_shadow"], light["resolution"], light["density"], cascades);
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

		//Entities the editor created from nothing (its Add/Entity): an entity that
		//comes from no model, no template and no other entity has nowhere else to be
		//recorded, so this section carries both its existence and everything that was
		//then added to it. Created *before* the "entities" phase below so an override
		//entry - or a wildcard rule - reaches one exactly as it reaches an
		//FBX-authored entity, and before "clones" so one can be a clone source.
		if (jw.contains("created_entities")) {
			for (const auto& created : jw["created_entities"]) {
				if (!created.contains("name") || !created["name"].is_string()) {
					continue;
				}
				std::string created_name = created["name"];
				float3 position{ 0.0f, 0.0f, 0.0f };
				if (created.contains("position")) {
					const auto& pos = created["position"];
					position = { pos["x"], pos["y"], pos["z"] };
				}
				float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
				if (created.contains("rotation")) {
					const auto& rot = created["rotation"];
					rotation = { rot["x"], rot["y"], rot["z"], rot["w"] };
				}
				float3 scale{ 1.0f, 1.0f, 1.0f };
				if (created.contains("scale")) {
					const auto& scl = created["scale"];
					scale = { scl["x"], scl["y"], scl["z"] };
				}
				ECS::Entity e = CreateEmptyEntity(created_name, position, rotation, scale);
				if (e == ECS::INVALID_ENTITY_ID) {
					continue;
				}
				//The components the entity was given after it was created. Applied after
				//the transform so a "components": {"Transform": ...} block still wins,
				//exactly as it does for a clone.
				ApplyComponents(e, created);
			}
		}

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
	//Init multitextures. This has to run *after* the material Init() loop above: a
	//layer's flags record which maps its source material actually carries, and those
	//textures only exist once the material has loaded them.
	ResolveMultiMaterials();
	for (auto& m : meshes.GetData()) {
		//Normal map textures are loaded from json file,
		//so we need to load in a second stage (after mesh.Init())
		m.LoadTextures();
	}

	//Init physics
	for (auto& e : coordinator->GetEntites()) {
		Components::Base& base = coordinator->GetComponent<Components::Base>(e.second);
		//Bounds as well as Mesh: a collider is sized from `local_box` and there is
		//nothing to size it from without one. Every entity an FBX or a template
		//produces has both, but one assembled component by component in the editor
		//(Add/Entity, then Mesh) may not - and GetComponent<Bounds> *throws*, out of
		//an Init() nothing catches, so the level that saved would not reopen.
		if (coordinator->ContainsComponent<Components::Mesh>(e.second) &&
			coordinator->ContainsComponent<Components::Bounds>(e.second)) {
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
			//An attached part's Transform holds an offset from its root, not a place in
			//the world, so its body goes where the composition puts it. For everything
			//else this is the entity's own pose, unchanged.
			float3 body_position = t.position;
			float4 body_rotation = t.rotation;
			ComposedWorldPose(e.second, body_position, body_rotation);
			if (p.body != nullptr) {
				//A body its own component block already created (Physics::FromJson builds
				//one so that a Physics block in a level record is not inert). Init-ing
				//again would leave that first body in the physics world with nothing
				//referencing it - a ghost collider the scene keeps colliding with.
				//
				//It is re-seated instead, because the block may well have been applied
				//before the Transform override in the same record: component blocks are
				//applied in key order, and "Physics" sorts before "Transform".
				std::lock_guard<std::recursive_mutex> lock(physics_mutex);
				reactphysics3d::Transform bt(
					{ body_position.x, body_position.y, body_position.z },
					{ body_rotation.x, body_rotation.y, body_rotation.z, body_rotation.w });
				p.body->setTransform(bt);
				p.last_body_transform = bt;
				p.UpdateShape(shape, b.local_box, t.scale, t.rotation);
				continue;
			}
			p.Init(phys_world, p.type, shape, b.local_box, body_position, t.scale, body_rotation, p.shape);
			
		}
	}	
	vertex_buffer->Prepare();
	for (auto& m : meshes.GetData()) {
		size_t offset = 0;
		bvh_buffer->Add(m.bvh.Root(), m.bvh.Size(), &m.bvhOffset);
	}
	bvh_buffer->Prepare();
	//The upload above is the first one, so it already carries whatever the level's
	//records re-smoothed on the way in.
	mesh_buffers_dirty = false;
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
				//Next to StaticMeshSystem, because it does the same job for the entities
				//that one excludes, and it needs the same two locks for the same reason.
				splat_cloud_system->Update(t.period, t.total);
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

