#include "ProjectBrowser.h"
#include "EditorLayout.h"

#include "imgui.h"

#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <Core/Json.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

using namespace nlohmann;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace ProjectBrowser {

		static std::string OpenLevelFileDialog(HWND owner)
		{
			char file[MAX_PATH] = {};
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = owner;
			ofn.lpstrFilter = "level.json\0level.json\0All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Open Level";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameA(&ofn)) {
				return std::string(file);
			}
			return std::string();
		}

		static std::string BrowseFolderDialog(HWND owner, const char* title)
		{
			BROWSEINFOA bi = {};
			bi.hwndOwner = owner;
			bi.lpszTitle = title;
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
			if (pidl == nullptr) {
				return std::string();
			}
			char path[MAX_PATH] = {};
			SHGetPathFromIDListA(pidl, path);
			CoTaskMemFree(pidl);
			return std::string(path);
		}

		//Walks up from a level.json path looking for a sibling config.json that marks
		//the project root, matching Marbles' own layout. Falls back to the level's
		//own directory if none is found (still lets the level open/save; just means
		//the Asset Browser won't find Assets/Objects for that project).
		std::string DeriveProjectRoot(const std::string& level_json_path)
		{
			fs::path dir = fs::path(level_json_path).parent_path();
			for (int i = 0; i < 8 && !dir.empty(); ++i) {
				if (fs::exists(dir / "config.json")) {
					return dir.string();
				}
				fs::path parent = dir.parent_path();
				if (parent == dir) break;
				dir = parent;
			}
			return fs::path(level_json_path).parent_path().string();
		}

		static void ScaffoldNewProject(const std::string& base_folder, std::string& out_level_path)
		{
			fs::path root(base_folder);
			fs::create_directories(root / "Assets" / "Levels" / "Solo" / "1");
			fs::create_directories(root / "Assets" / "Materials");
			fs::create_directories(root / "Assets" / "Objects");
			fs::create_directories(root / "Assets" / "Audio");
			fs::create_directories(root / "Assets" / "Ui");

			json config;
			config["ui"]["root"] = "Assets\\Ui\\";
			config["objects"]["root"] = "Assets\\Objects\\";
			config["solo"]["root"] = "Assets\\Levels\\Solo\\";
			config["solo"]["levels"] = json::array({ { {"id", 1}, {"name", "Level 1"} } });
			std::ofstream config_out((root / "config.json").string());
			config_out << config.dump(4);
			config_out.close();

			//Minimal, instances-only starting scene: no "level" FBX yet (engine-level
			//support for an optional base FBX, see World::Load), no instances yet.
			//World::Load resolves "path" against the process's working directory when
			//relative, so a freshly scaffolded project (which can live anywhere on disk)
			//gets an absolute path here rather than a Marbles-style relative one.
			json level;
			level["world"]["path"] = (root / "Assets").string() + "\\";
			level["world"]["lights"] = json::array({
				{ {"type", "ambient"}, {"name", "ambient"}, {"color_up", "050050050"}, {"color_down", "020020020"} }
			});
			level["world"]["entities"] = json::array();
			level["world"]["instances"] = json::array();
			level["world"]["templates"] = json::array();
			level["world"]["material_files"] = json::array();

			out_level_path = (root / "Assets" / "Levels" / "Solo" / "1" / "level.json").string();
			std::ofstream level_out(out_level_path);
			level_out << level.dump(4);
			level_out.close();
		}

		static void ListLevelsFromConfig(const std::string& project_root, std::vector<std::pair<std::string, std::string>>& out_levels)
		{
			out_levels.clear();
			fs::path config_path = fs::path(project_root) / "config.json";
			if (fs::exists(config_path)) {
				try {
					json config = json::parse(std::ifstream(config_path.string()));
					if (config.contains("solo") && config["solo"].contains("levels") && config["solo"].contains("root")) {
						std::string levels_root = config["solo"]["root"];
						for (auto& lvl : config["solo"]["levels"]) {
							std::string id = std::to_string((int)lvl["id"]);
							std::string name = lvl.value("name", ("Level " + id));
							fs::path level_json = fs::path(project_root) / levels_root / id / "level.json";
							if (fs::exists(level_json)) {
								out_levels.push_back({ name, level_json.string() });
							}
						}
					}
				}
				catch (...) {
					//Fall through to the filesystem scan below.
				}
			}
			if (out_levels.empty()) {
				//No usable config.json level list: fall back to scanning for any level.json
				//under Assets/Levels, so the browser still works against ad hoc layouts.
				fs::path levels_dir = fs::path(project_root) / "Assets" / "Levels";
				if (fs::exists(levels_dir)) {
					for (auto& entry : fs::recursive_directory_iterator(levels_dir)) {
						if (entry.is_regular_file() && entry.path().filename() == "level.json") {
							out_levels.push_back({ entry.path().parent_path().filename().string(), entry.path().string() });
						}
					}
				}
			}
		}

		void OpenLevelWithDialog(EditorState& state, SceneEditorApp& app)
		{
			std::string path = OpenLevelFileDialog(app.wnd);
			if (!path.empty()) {
				state.project_root = DeriveProjectRoot(path);
				app.OpenLevel(path);
			}
		}

		void NewProjectWithDialog(EditorState& state, SceneEditorApp& app)
		{
			std::string base = BrowseFolderDialog(app.wnd, "Choose an empty folder for the new project");
			if (!base.empty()) {
				std::string level_path;
				ScaffoldNewProject(base, level_path);
				state.project_root = base;
				app.OpenLevel(level_path);
			}
		}

		void Draw(EditorState& state, SceneEditorApp& app)
		{
			EditorLayout::PlaceProject(state);
			ImGui::Begin("Project");

			if (!state.project_root.empty()) {
				ImGui::TextWrapped("Project: %s", state.project_root.c_str());
			}
			else {
				ImGui::TextUnformatted("No project open.");
				ImGui::TextUnformatted("Use File > Open Level... or File > New Project... to get started.");
			}
			if (!state.current_level_path.empty()) {
				ImGui::TextWrapped("Level: %s", state.current_level_path.c_str());
			}

			if (!state.project_root.empty()) {
				std::vector<std::pair<std::string, std::string>> levels;
				ListLevelsFromConfig(state.project_root, levels);
				if (!levels.empty()) {
					ImGui::SeparatorText("Levels");
					for (auto& [name, path] : levels) {
						if (ImGui::Button(name.c_str())) {
							app.OpenLevel(path);
						}
					}
				}
			}

			ImGui::End();
		}

	}
}
