#include "AssetBrowser.h"

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
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
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
			for (auto& t : state.templates) {
				if (!t.loaded) {
					state.world->LoadTemplate(t.file_path, false, false);
					t.loaded = true;
				}
			}
		}

		void Draw(EditorState& state)
		{
			ImGui::Begin("Asset Browser");

			if (state.project_root.empty()) {
				ImGui::TextUnformatted("No project open.");
				ImGui::End();
				return;
			}

			static std::string scanned_root;
			if (scanned_root != state.project_root) {
				ScanObjectsFolder(state);
				scanned_root = state.project_root;
			}

			if (ImGui::Button("Import Object...")) {
				HWND owner = nullptr; //ImGui doesn't own a native HWND handle here; nullptr is a valid dialog owner.
				std::string picked = OpenFbxFileDialog(owner);
				if (!picked.empty()) {
					fs::path dest_dir = fs::path(state.project_root) / "Assets" / "Objects";
					fs::create_directories(dest_dir);
					fs::path dest = dest_dir / fs::path(picked).filename();
					if (!fs::exists(dest) || fs::equivalent(fs::path(picked), dest) == false) {
						std::error_code ec;
						fs::copy_file(picked, dest, fs::copy_options::overwrite_existing, ec);
					}

					TemplateAsset asset;
					asset.name = dest.filename().replace_extension().string();
					asset.file_path = dest.string();
					asset.newly_imported = true;
					state.world->LoadTemplate(asset.file_path, false, false);
					asset.loaded = true;
					state.templates.push_back(asset);
					state.selected_template = asset.name;
					state.status_message = "Imported: " + asset.name;
				}
			}

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
				static int place_counter = 0;
				std::string instance_name = state.selected_template + "_inst_" + std::to_string(place_counter++);

				float3 position{ 0.0f, 0.0f, 0.0f };
				float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
				float3 scale{ 1.0f, 1.0f, 1.0f };

				ECS::Entity e = state.world->SpawnInstance(instance_name, state.selected_template, position, rotation, scale);
				if (e != ECS::INVALID_ENTITY_ID) {
					PlacedInstance inst;
					inst.name = instance_name;
					inst.template_name = state.selected_template;
					inst.position = position;
					inst.rotation = rotation;
					inst.scale = scale;
					state.placed_instances.push_back(inst);
					state.instance_entity_ids.insert(e);
					state.selected_entity = e;
					state.status_message = "Placed: " + instance_name;
				}
			}
			ImGui::EndDisabled();

			ImGui::End();
		}

	}
}
