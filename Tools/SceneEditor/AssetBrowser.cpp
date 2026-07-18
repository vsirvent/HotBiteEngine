#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "Inspector.h"

#include "imgui.h"

#include <Windows.h>
#include <commdlg.h>
#include <filesystem>

#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;
using namespace HotBite::Engine;

namespace HotBiteEditor {
	namespace AssetBrowser {

		static std::string OpenFbxFileDialog(HWND owner)
		{
			char file[MAX_PATH] = {};
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = owner;
			ofn.lpstrFilter = "FBX files\0*.fbx\0All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Import Object";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameA(&ofn)) {
				return std::string(file);
			}
			return std::string();
		}

		static void ScanObjectsFolder(EditorState& state)
		{
			fs::path objects_dir = fs::path(state.project_root) / "Assets" / "Objects";
			if (!fs::exists(objects_dir)) {
				return;
			}
			for (auto& entry : fs::directory_iterator(objects_dir)) {
				if (!entry.is_regular_file() || entry.path().extension() != ".fbx") {
					continue;
				}
				std::string name = entry.path().filename().replace_extension().string();
				bool already_known = false;
				for (auto& t : state.templates) {
					if (t.name == name) { already_known = true; break; }
				}
				if (!already_known) {
					TemplateAsset asset;
					asset.name = name;
					asset.file_path = entry.path().string();
					state.templates.push_back(asset);
				}
			}
			//Make every discovered template immediately usable for placement this session.
			//Templates the level.json already loaded (their World registry key is the
			//filename stem, same as our TemplateAsset name) must NOT be loaded again:
			//World's dedup is by path string, and re-loading under our absolute path
			//replaces the registered template entities/meshes with copies whose vertex
			//data was never uploaded to the GPU (Init() ran before this scan), which
			//makes every instance placed from them render as nothing.
			bool loaded_any = false;
			for (auto& t : state.templates) {
				if (!t.loaded) {
					if (!state.world->IsTemplateLoaded(t.name)) {
						state.world->LoadTemplate(t.file_path, false, false);
						loaded_any = true;
					}
					t.loaded = true;
				}
			}
			if (loaded_any) {
				//Scan runs after World::Init has already uploaded the GPU buffers.
				state.world->RefreshMeshBuffers();
			}
		}

		void EnsureTemplatesScanned(EditorState& state)
		{
			static std::string scanned_root;
			if (!state.project_root.empty() && scanned_root != state.project_root) {
				ScanObjectsFolder(state);
				scanned_root = state.project_root;
			}
		}

		bool ImportObject(EditorState& state, const std::string& fbx_path, std::string& error)
		{
			if (state.project_root.empty()) {
				error = "no project open";
				return false;
			}
			if (!fs::exists(fbx_path)) {
				error = "file not found: " + fbx_path;
				return false;
			}
			fs::path dest_dir = fs::path(state.project_root) / "Assets" / "Objects";
			fs::create_directories(dest_dir);
			fs::path dest = dest_dir / fs::path(fbx_path).filename();
			if (!fs::exists(dest) || fs::equivalent(fs::path(fbx_path), dest) == false) {
				std::error_code ec;
				fs::copy_file(fbx_path, dest, fs::copy_options::overwrite_existing, ec);
			}

			TemplateAsset asset;
			asset.name = dest.filename().replace_extension().string();
			asset.file_path = dest.string();
			asset.newly_imported = true;
			state.world->LoadTemplate(asset.file_path, false, false);
			//Imports happen while the session is running, i.e. after World::Init
			//uploaded the GPU buffers; re-upload so the new mesh can render.
			state.world->RefreshMeshBuffers();
			asset.loaded = true;
			state.templates.push_back(asset);
			state.selected_template = asset.name;
			state.status_message = "Imported: " + asset.name;
			return true;
		}

		//Spawns `inst` into the world and registers the save/selection bookkeeping.
		//Shared by the user-facing PlaceTemplate, paste (EntityOps) and the undo/redo
		//closures of both, so every path runs exactly the original placement code.
		bool SpawnRecordedInstance(EditorState& state, const PlacedInstance& inst, std::string& error)
		{
			ECS::Entity e = state.world->SpawnInstance(inst.name, inst.template_name,
				inst.position, inst.rotation, inst.scale, inst.material_name);
			if (e == ECS::INVALID_ENTITY_ID) {
				error = "SpawnInstance failed for template: " + inst.template_name;
				return false;
			}
			state.placed_instances.push_back(inst);
			state.instance_entity_ids.insert(e);
			state.selected_entity = e;
			Inspector::RefreshEulerCache(state);
			return true;
		}

		//Undo of a place / cut of an instance: destroys the instance's entities
		//(every part of a multi-part template) and drops its bookkeeping. Placed
		//instances never carry a Physics component, so DestroyEntity fully cleans
		//them up.
		void RemovePlacedInstance(EditorState& state, const std::string& instance_name)
		{
			auto record = state.placed_instances.end();
			for (auto it = state.placed_instances.begin(); it != state.placed_instances.end(); ++it) {
				if (it->name == instance_name) {
					record = it;
					break;
				}
			}
			ECS::Coordinator* c = state.world->GetCoordinator();
			if (record == state.placed_instances.end() || c == nullptr) {
				return;
			}
			//SpawnInstance names multi-part instances "<name>_<index>" per part and
			//single-part ones plain "<name>"; mirror that to find every entity.
			size_t parts = state.world->GetTemplateEntities(record->template_name).size();
			std::vector<std::string> part_names;
			if (parts > 1) {
				for (size_t i = 0; i < parts; ++i) {
					part_names.push_back(instance_name + "_" + std::to_string(i));
				}
			}
			else {
				part_names.push_back(instance_name);
			}
			for (const auto& part : part_names) {
				ECS::Entity e = c->GetEntityByName(part);
				if (e == ECS::INVALID_ENTITY_ID) {
					continue;
				}
				if (state.selected_entity == e) {
					state.selected_entity = ECS::INVALID_ENTITY_ID;
				}
				state.instance_entity_ids.erase(e);
				c->DestroyEntity(e);
			}
			state.placed_instances.erase(record);
		}

		bool PlaceTemplate(EditorState& state, const std::string& template_name, std::string& error)
		{
			bool known = false;
			for (auto& t : state.templates) {
				if (t.name == template_name) { known = true; break; }
			}
			if (!known) {
				error = "unknown template: " + template_name;
				return false;
			}

			static int place_counter = 0;
			PlacedInstance inst;
			inst.name = template_name + "_inst_" + std::to_string(place_counter++);
			inst.template_name = template_name;

			if (!SpawnRecordedInstance(state, inst, error)) {
				return false;
			}
			state.status_message = "Placed: " + inst.name;
			EditorHistory::Push({
				"place " + inst.name,
				[inst](EditorState& s) {
					RemovePlacedInstance(s, inst.name);
				},
				[inst](EditorState& s) {
					std::string err;
					SpawnRecordedInstance(s, inst, err);
				} });
			return true;
		}

		void ImportObjectWithDialog(EditorState& state)
		{
			HWND owner = nullptr; //ImGui doesn't own a native HWND handle here; nullptr is a valid dialog owner.
			std::string picked = OpenFbxFileDialog(owner);
			if (!picked.empty()) {
				std::string error;
				if (!ImportObject(state, picked, error)) {
					state.status_message = "Import failed: " + error;
				}
			}
		}

		void Draw(EditorState& state)
		{
			ImGui::Begin(EditorLayout::ASSET_BROWSER_WINDOW);

			if (state.project_root.empty()) {
				ImGui::TextUnformatted("No project open.");
				ImGui::End();
				return;
			}

			EnsureTemplatesScanned(state);

			ImGui::SeparatorText("Templates");
			for (auto& t : state.templates) {
				bool is_selected = (t.name == state.selected_template);
				if (ImGui::Selectable(t.name.c_str(), is_selected)) {
					state.selected_template = t.name;
				}
			}

			ImGui::Separator();
			ImGui::BeginDisabled(state.selected_template.empty());
			if (ImGui::Button("Place at Origin")) {
				std::string error;
				if (!PlaceTemplate(state, state.selected_template, error)) {
					state.status_message = "Place failed: " + error;
				}
			}
			ImGui::EndDisabled();

			ImGui::End();
		}

	}
}
