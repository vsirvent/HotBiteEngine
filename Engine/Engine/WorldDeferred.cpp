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

// Assets loaded on first use (World.h, "Assets loaded on first use"). A level registered with DeferAssets keeps only its lists in memory;
// a template, and what it needs, is read in the first time something spawns it. Everything here goes through the same calls a full load
// makes (LoadModel, MaterialData::Load, LoadGeneratedMeshesFrom, CreateTemplate) - only when it happens differs - so a deferred template
// ends up exactly as it would have been loaded with the level.

#include "World.h"
#include <Core/GfxDevice.h>
#include <Core/Material.h>
#include <Loader/CookedModel.h>
#include <Loader/ModelCache.h>
#include <Systems/RenderSystem.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using json = nlohmann::json;

namespace {
	// Bumped when what the index records changes; an index of another version is rebuilt.
	constexpr int INDEX_VERSION = 1;

	// The meshes, materials and referenced part templates a template definition needs: its Mesh block (the mesh, its levels of detail,
	// its ray-tracing stand-in), its Material, and the same for each inline part, recursively. A part that names another template
	// brings that template in (loaded with it: spawning one spawns the other at once).
	void CollectNeeds(const json& components, const json& parts, std::set<std::string>& meshes, std::set<std::string>& materials,
		std::set<std::string>& part_templates) {
		if (components.is_object()) {
			auto mesh = components.find("Mesh");
			if (mesh != components.end() && mesh->is_object()) {
				const std::string name = mesh->value("name", std::string());
				if (!name.empty()) { meshes.insert(name); }
				auto lods = mesh->find("lods");
				if (lods != mesh->end() && lods->is_array()) {
					for (const json& lod : *lods) {
						const std::string lod_name = lod.is_object() ? lod.value("name", std::string()) : std::string();
						if (!lod_name.empty()) { meshes.insert(lod_name); }
					}
				}
				const std::string trace = mesh->value("trace_lod", std::string());
				if (!trace.empty()) { meshes.insert(trace); }
			}
			auto material = components.find("Material");
			if (material != components.end() && material->is_object()) {
				const std::string name = material->value("name", std::string());
				if (!name.empty()) { materials.insert(name); }
			}
		}
		if (parts.is_array()) {
			for (const json& part : parts) {
				if (!part.is_object()) { continue; }
				auto referenced = part.find("template");
				if (referenced != part.end() && referenced->is_string() && !referenced->get<std::string>().empty()) {
					part_templates.insert(referenced->get<std::string>());
					continue;
				}
				auto inner = part.find("components");
				if (inner != part.end() && inner->is_object()) {
					auto inner_parts = part.find("parts");
					CollectNeeds(*inner, inner_parts != part.end() ? *inner_parts : json::array(), meshes, materials, part_templates);
				}
			}
		}
	}

	// A cooked file's size and last-write time, which is what says whether the index still describes it.
	bool Stamp(const std::string& file, uint64_t& size, int64_t& time) {
		std::error_code ec;
		size = (uint64_t)std::filesystem::file_size(file, ec);
		if (ec) { return false; }
		const auto when = std::filesystem::last_write_time(file, ec);
		if (ec) { return false; }
		time = (int64_t)when.time_since_epoch().count();
		return true;
	}

	double MillisSince(std::chrono::steady_clock::time_point start) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
}

void World::DeferAssets(const json& world_json, const std::string& root, const std::string& index_file) {
	const auto begin = std::chrono::steady_clock::now();
	auto c = GetCoordinator();
	std::scoped_lock render_lock(c->GetSystem<RenderSystem>()->mutex);
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	auto on_materials = std::move(deferred.on_materials);
	deferred = DeferredAssets();
	deferred.on_materials = std::move(on_materials);
	deferred.root = root;

	// The models, and which meshes and materials each holds: the index, remade for a model whose cooked file is not what it describes.
	json index;
	{
		std::ifstream in(index_file);
		if (in) {
			try { index = json::parse(in); }
			catch (...) { index = json(); }
		}
	}
	if (!index.is_object() || index.value("version", 0) != INDEX_VERSION || !index.contains("models") || !index["models"].is_object()) {
		index = json{ { "version", INDEX_VERSION }, { "models", json::object() } };
	}
	bool index_changed = false;
	std::vector<size_t> eager;
	for (const json& m : world_json.value("models", json::array())) {
		const std::string file = m.value("file", std::string());
		if (file.empty()) { continue; }
		DeferredAssets::Model model;
		model.file = file;
		model.name = m.value("name", std::string());
		model.triangulate = m.value("triangulate", false);
		const size_t i = deferred.models.size();
		deferred.models.push_back(model);
		deferred.model_loaded.push_back(false);
		deferred.model_materials.emplace_back();

		const std::string full = root + file;
		std::string ext = std::filesystem::path(file).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
		if (ext != ".fbx") {
			// Not a model the index can describe (a splat cloud): loaded now, as a full load would.
			eager.push_back(i);
			continue;
		}
		const std::string cooked = Loader::CookedModel::PathFor(full);
		std::error_code ec;
		// A model with no cooked file is cooked now, through the model cache (it reads the .fbx once, writes the cooked file and
		// counts both, like any first load), and described from what that read.
		std::shared_ptr<Loader::CookedModel> fresh;
		if (!std::filesystem::exists(cooked, ec)) {
			fresh = Loader::AcquireModel(full, model.triangulate, model.use_animation_names);
		}
		uint64_t size = 0;
		int64_t time = 0;
		json& entry = index["models"][file];
		const bool current = fresh == nullptr && Stamp(cooked, size, time) && entry.is_object() &&
			entry.value("cooked_size", (uint64_t)0) == size && entry.value("cooked_time", (int64_t)0) == time;
		if (!current) {
			Stamp(cooked, size, time);
			Loader::CookedModel read;
			std::string error;
			if (fresh != nullptr) {
				read = std::move(*fresh);
				fresh.reset();
			}
			else if (!Loader::CookedModel::Read(cooked, read, &error)) {
				printf("World::DeferAssets: cannot read the cooked model of '%s' (%s): loading it now\n", file.c_str(), error.c_str());
				index["models"].erase(file);
				eager.push_back(i);
				continue;
			}
			entry = json::object();
			entry["cooked_size"] = size;
			entry["cooked_time"] = time;
			json mesh_names = json::array();
			for (const Loader::CookedMesh& mesh : read.meshes) { mesh_names.push_back(mesh.name); }
			json material_names = json::array();
			for (const Loader::CookedMaterial& material : read.materials) { material_names.push_back(material.name); }
			entry["meshes"] = std::move(mesh_names);
			entry["materials"] = std::move(material_names);
			index_changed = true;
		}
		const json& meshes_of = entry["meshes"];
		for (const json& mesh : meshes_of) {
			deferred.mesh_model.emplace(mesh.get<std::string>(), i);
		}
		for (const json& material : entry["materials"]) {
			deferred.model_materials[i].push_back(material.get<std::string>());
		}
		// An animation set (no mesh): no template names it, a clip does - so it is there from the start.
		if (meshes_of.empty()) {
			eager.push_back(i);
		}
	}
	if (index_changed) {
		std::ofstream out(index_file);
		if (out) {
			out << index.dump(1);
		}
	}

	// The material files: their materials and layer stacks by name, and each file's texture root (what an editor writes them back with).
	for (const json& file_json : world_json.value("material_files", json::array())) {
		if (!file_json.is_string()) { continue; }
		const std::string file = file_json.get<std::string>();
		json scene;
		try {
			scene = json::parse(std::ifstream(root + file));
		}
		catch (const std::exception& ex) {
			printf("World::DeferAssets: cannot read material file %s: %s\n", (root + file).c_str(), ex.what());
			continue;
		}
		const std::string texture_root = root + scene.value("root", std::string());
		material_files[file] = texture_root;
		for (const json& m : scene.value("materials", json::array())) {
			const std::string name = m.value("name", std::string());
			if (!name.empty()) {
				deferred.materials[name] = DeferredMaterial{ m, texture_root, file, false };
			}
		}
		for (const json& mm : scene.value("multi_materials", json::array())) {
			if (mm.contains("textures") && mm.contains("name") && mm["name"].is_string()) {
				deferred.multi_materials[mm["name"].get<std::string>()] = { mm, file };
			}
		}
	}

	for (const json& entry : world_json.value("generated_meshes", json::array())) {
		const std::string name = entry.value("name", std::string());
		if (!name.empty()) {
			deferred.generated[name] = entry;
		}
	}

	for (const json& t : world_json.value("templates", json::array())) {
		const std::string file = t.value("file", std::string());
		if (file.empty()) { continue; }
		std::string name;
		std::string error;
		json components;
		json parts;
		if (ReadTemplateFile(root + file, false, name, components, parts, error)) {
			deferred.templates[name] = DeferredTemplate{ components, parts, false };
		}
		else {
			printf("World::DeferAssets: %s\n", error.c_str());
		}
	}

	DeferredBatch batch;
	batch.meshes_before = meshes.GetData().size();
	for (size_t i : eager) {
		LoadDeferredModel(i, batch);
	}
	FinishDeferredLoad(batch);
	printf("World::DeferAssets: %zu templates, %zu models (%zu loaded now), %zu materials, %zu generated meshes registered in %.0f ms\n",
		deferred.templates.size(), deferred.models.size(), eager.size(), deferred.materials.size(), deferred.generated.size(),
		MillisSince(begin));
	NoteMemory("World::DeferAssets");
}

bool World::LoadDeferredModel(size_t index, DeferredBatch& batch) {
	if (index >= deferred.models.size()) {
		return false;
	}
	if (deferred.model_loaded[index]) {
		return true;
	}
	deferred.model_loaded[index] = true;
	const DeferredAssets::Model& model = deferred.models[index];
	LoadModel(deferred.root + model.file, model.triangulate, false, model.use_animation_names, model.name);
	batch.meshes_added = true;
	// The material files' versions of what the model brought in, laid over it as a full load lays the files over the models.
	for (const std::string& material : deferred.model_materials[index]) {
		LoadDeferredMaterial(material, batch);
	}
	return true;
}

bool World::LoadDeferredMesh(const std::string& name, DeferredBatch& batch) {
	if (meshes.Get(name) != nullptr) {
		return true;
	}
	auto generated = deferred.generated.find(name);
	if (generated != deferred.generated.end()) {
		if (deferred.generated_loaded.insert(name).second) {
			// A level of detail is made from its source mesh, which comes first.
			LoadDeferredMesh(generated->second.value("source", std::string()), batch);
			LoadGeneratedMeshesFrom(json::array({ generated->second }), deferred.root);
			batch.meshes_added = true;
		}
		return meshes.Get(name) != nullptr;
	}
	auto model = deferred.mesh_model.find(name);
	if (model != deferred.mesh_model.end()) {
		LoadDeferredModel(model->second, batch);
		return meshes.Get(name) != nullptr;
	}
	return false;
}

bool World::LoadDeferredMaterial(const std::string& name, DeferredBatch& batch) {
	auto it = deferred.materials.find(name);
	if (it == deferred.materials.end()) {
		return materials.Get(name) != nullptr;
	}
	DeferredMaterial& entry = it->second;
	if (entry.loaded) {
		return true;
	}
	entry.loaded = true;
	// Its textures read on every core first (the material then takes them from the cache one by one).
	PreloadTextures(CollectMaterialTextures(json::array({ entry.node }), entry.texture_root));
	MaterialData* material = materials.Get(name);
	if (material == nullptr) {
		materials.Insert(name, MaterialData{ name });
		material = materials.Get(name);
	}
	material->Load(entry.texture_root, entry.node.dump());
	material_origin[name] = entry.file;
	batch.materials.push_back(name);
	// The layer stack it names, and the materials that stack is made of.
	const std::string stack = entry.node.value("multi_material", std::string());
	auto mm = deferred.multi_materials.find(stack);
	if (!stack.empty() && mm != deferred.multi_materials.end() && multi_material_origin.find(stack) == multi_material_origin.end()) {
		for (const json& layer : mm->second.first.value("textures", json::array())) {
			if (!layer.is_object()) { continue; }
			for (auto field = layer.begin(); field != layer.end(); ++field) {
				if (field->is_string() && deferred.materials.count(field->get<std::string>()) != 0) {
					LoadDeferredMaterial(field->get<std::string>(), batch);
				}
			}
		}
		LoadMultiMaterial(stack, mm->second.first);
		multi_material_origin[stack] = mm->second.second;
		batch.multi_materials = true;
	}
	return true;
}

bool World::LoadDeferredTemplate(const std::string& name, DeferredBatch& batch, std::set<std::string>& visiting) {
	auto it = deferred.templates.find(name);
	if (it == deferred.templates.end()) {
		return IsTemplateLoaded(name);
	}
	if (it->second.loaded) {
		return true;
	}
	if (!visiting.insert(name).second) {
		return false;
	}
	std::set<std::string> mesh_names;
	std::set<std::string> material_names;
	std::set<std::string> part_templates;
	CollectNeeds(it->second.components, it->second.parts, mesh_names, material_names, part_templates);
	for (const std::string& mesh : mesh_names) {
		if (!LoadDeferredMesh(mesh, batch)) {
			printf("World: template '%s' names mesh '%s', which no model or generated mesh of the level has\n", name.c_str(), mesh.c_str());
		}
	}
	for (const std::string& material : material_names) {
		LoadDeferredMaterial(material, batch);
	}
	for (const std::string& part : part_templates) {
		LoadDeferredTemplate(part, batch, visiting);
	}
	std::string error;
	it->second.loaded = true;
	if (!CreateTemplate(name, it->second.components, it->second.parts, error)) {
		printf("World: deferred template '%s' could not be created: %s\n", name.c_str(), error.c_str());
		return false;
	}
	return true;
}

void World::FinishDeferredLoad(DeferredBatch& batch) {
	// The game's copies of the new materials (team colours, effect variants), made before anything is drawn with them.
	if (!batch.materials.empty() && deferred.on_materials) {
		deferred.on_materials(batch.materials);
	}
	if (batch.multi_materials) {
		ResolveMultiMaterials();
	}
	// What the parallel preloads read and no material took.
	ReleaseUnclaimedTextures();
	if (!scene_init) {
		// World::Init initializes and uploads everything there is when it runs.
		return;
	}
	for (MaterialData& material : materials.GetData()) {
		if (!material.init) {
			material.Init();
		}
	}
	if (batch.meshes_added) {
		std::vector<MeshData>& data = meshes.GetData();
		for (size_t i = batch.meshes_before; i < data.size(); ++i) {
			data[i].LoadTextures();
		}
		RefreshMeshBuffers();
	}
}

bool World::EnsureTemplate(const std::string& name) {
	{
		std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
		auto it = deferred.templates.find(name);
		if (it == deferred.templates.end() || it->second.loaded) {
			return IsTemplateLoaded(name);
		}
	}
	const auto begin = std::chrono::steady_clock::now();
	// Nothing is drawn while meshes, materials and the world's buffers change (always the render lock first, then this one).
	auto c = GetCoordinator();
	std::scoped_lock render_lock(c->GetSystem<RenderSystem>()->mutex);
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	DeferredBatch batch;
	batch.meshes_before = meshes.GetData().size();
	std::set<std::string> visiting;
	const bool ok = LoadDeferredTemplate(name, batch, visiting);
	FinishDeferredLoad(batch);
	const double ms = MillisSince(begin);
	deferred.load_ms += ms;
	if (ms > deferred.longest_ms) {
		deferred.longest_ms = ms;
		deferred.longest = name;
	}
	printf("World: template '%s' loaded on first use in %.0f ms\n", name.c_str(), ms);
	NoteMemory(("template " + name).c_str());
	return ok && IsTemplateLoaded(name);
}

MaterialData* World::EnsureMaterial(const std::string& name) {
	{
		std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
		auto it = deferred.materials.find(name);
		if (it == deferred.materials.end() || it->second.loaded) {
			return materials.Get(name);
		}
	}
	const auto begin = std::chrono::steady_clock::now();
	auto c = GetCoordinator();
	std::scoped_lock render_lock(c->GetSystem<RenderSystem>()->mutex);
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	DeferredBatch batch;
	batch.meshes_before = meshes.GetData().size();
	LoadDeferredMaterial(name, batch);
	FinishDeferredLoad(batch);
	deferred.load_ms += MillisSince(begin);
	return materials.Get(name);
}

bool World::EnsureMesh(const std::string& name) {
	if (meshes.Get(name) != nullptr) {
		return true;
	}
	const auto begin = std::chrono::steady_clock::now();
	auto c = GetCoordinator();
	std::scoped_lock render_lock(c->GetSystem<RenderSystem>()->mutex);
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	DeferredBatch batch;
	batch.meshes_before = meshes.GetData().size();
	const bool ok = LoadDeferredMesh(name, batch);
	FinishDeferredLoad(batch);
	deferred.load_ms += MillisSince(begin);
	return ok;
}

void World::EnsureAllAssets() {
	const auto begin = std::chrono::steady_clock::now();
	auto c = GetCoordinator();
	std::scoped_lock render_lock(c->GetSystem<RenderSystem>()->mutex);
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	DeferredBatch batch;
	batch.meshes_before = meshes.GetData().size();
	for (size_t i = 0; i < deferred.models.size(); ++i) {
		LoadDeferredModel(i, batch);
	}
	std::vector<std::string> names;
	for (const auto& [name, entry] : deferred.materials) { names.push_back(name); }
	for (const std::string& name : names) { LoadDeferredMaterial(name, batch); }
	names.clear();
	for (const auto& [name, entry] : deferred.generated) { names.push_back(name); }
	for (const std::string& name : names) { LoadDeferredMesh(name, batch); }
	std::set<std::string> visiting;
	for (auto& [name, entry] : deferred.templates) {
		LoadDeferredTemplate(name, batch, visiting);
	}
	FinishDeferredLoad(batch);
	deferred.load_ms += MillisSince(begin);
	NoteMemory("World::EnsureAllAssets");
}

bool World::IsTemplateAvailable(const std::string& name) const {
	if (template_entities.find(name) != template_entities.end()) {
		return true;
	}
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	return deferred.templates.find(name) != deferred.templates.end();
}

World::DeferredStats World::GetDeferredStats() const {
	std::lock_guard<std::recursive_mutex> lock(deferred_mutex);
	DeferredStats stats;
	stats.templates = deferred.templates.size();
	for (const auto& [name, entry] : deferred.templates) {
		stats.templates_loaded += entry.loaded ? 1 : 0;
	}
	stats.models = deferred.models.size();
	for (bool loaded : deferred.model_loaded) {
		stats.models_loaded += loaded ? 1 : 0;
	}
	stats.materials = deferred.materials.size();
	for (const auto& [name, entry] : deferred.materials) {
		stats.materials_loaded += entry.loaded ? 1 : 0;
	}
	stats.generated = deferred.generated.size();
	stats.generated_loaded = deferred.generated_loaded.size();
	stats.load_ms = deferred.load_ms;
	stats.longest_ms = deferred.longest_ms;
	stats.longest = deferred.longest;
	return stats;
}
