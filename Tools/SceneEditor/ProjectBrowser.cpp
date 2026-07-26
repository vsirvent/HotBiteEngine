#include "ProjectBrowser.h"

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
			//A level is any .json the editor can load, not necessarily one named
			//"level.json" - DemoGame's levels are demo.json, marbles' are level.json.
			ofn.lpstrFilter = "JSON files (*.json)\0*.json\0All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrDefExt = "json";
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

		void OpenLevelWithDialog(EditorState& state, SceneEditorApp& app)
		{
			std::string path = OpenLevelFileDialog(app.wnd);
			if (!path.empty()) {
				state.project_root = DeriveProjectRoot(path);
				//Queued rather than loaded here: this runs from the File menu, i.e.
				//inside an ImGui frame, and the load paints progress frames of its own
				//(see SceneEditorApp::OpenLevel).
				app.RequestOpenLevel(path);
			}
		}

		void NewProjectWithDialog(EditorState& state, SceneEditorApp& app)
		{
			std::string base = BrowseFolderDialog(app.wnd, "Choose an empty folder for the new project");
			if (!base.empty()) {
				std::string level_path;
				ScaffoldNewProject(base, level_path);
				state.project_root = base;
				//Same as OpenLevelWithDialog: deferred out of the menu's ImGui frame.
				app.RequestOpenLevel(level_path);
			}
		}

	}
}
