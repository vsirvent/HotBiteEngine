#include "AssetBrowser.h"
#include "EditorLayout.h"

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
			std::string instance_name = template_name + "_inst_" + std::to_string(place_counter++);

			float3 position{ 0.0f, 0.0f, 0.0f };
			float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
			float3 scale{ 1.0f, 1.0f, 1.0f };

			ECS::Entity e = state.world->SpawnInstance(instance_name, template_name, position, rotation, scale);
			if (e == ECS::INVALID_ENTITY_ID) {
				error = "SpawnInstance failed for template: " + template_name;
				return false;
			}
			PlacedInstance inst;
			inst.name = instance_name;
			inst.template_name = template_name;
			inst.position = position;
			inst.rotation = rotation;
			inst.scale = scale;
			state.placed_instances.push_back(inst);
			state.instance_entity_ids.insert(e);
			state.selected_entity = e;
			state.status_message = "Placed: " + instance_name;
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
			EditorLayout::PlaceAssetBrowser(state);
			ImGui::Begin("Asset Browser");

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
