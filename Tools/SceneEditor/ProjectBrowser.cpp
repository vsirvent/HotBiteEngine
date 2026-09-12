#include "ProjectBrowser.h"
#include "RenderSettings.h"
#include "SceneSerializer.h"

#include <Windows.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <Core/Json.h>

#pragma comment(lib, "comdlg32.lib")

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

		//Shared by New Level and Save As: a native Save dialog that hands back
		//wherever the user chose, with no assumption about what is already there -
		//the folder can be new, existing and empty, or existing and full of other
		//projects' files, and every one of those is a valid answer.
		static std::string SaveLevelFileDialog(HWND owner, const char* title, const std::string& default_name)
		{
			char file[MAX_PATH] = {};
			strncpy_s(file, default_name.c_str(), sizeof(file) - 1);
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = owner;
			ofn.lpstrFilter = "JSON files (*.json)\0*.json\0All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrDefExt = "json";
			ofn.lpstrTitle = title;
			ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
			if (GetSaveFileNameA(&ofn)) {
				return std::string(file);
			}
			return std::string();
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

		//Writes a minimal, instances-only starting level at exactly the path the user
		//chose - no folder tree beyond that path's own parent (created if it does not
		//exist yet), no config.json, no placeholder Materials/Audio/Ui folders. Those
		//used to be scaffolded unconditionally, in a fixed Assets/Levels/Solo/1 nesting
		//copied from Marbles' own campaign layout, whether or not this project has
		//anything to do with a Marbles-style level select screen; none of it is
		//something the editor or engine actually requires.
		//
		//The "Assets" folder itself (for imported models, and now for the default
		//material file below) is created up front rather than left for the first
		//import: World::Load only reads "path" out of the level for asset resolution,
		//and AssetBrowser::ImportModel would otherwise create Assets/Objects itself
		//the first time something is actually imported. Naming it "Assets" (rather
		//than, say, putting models next to the level file) is kept only because
		//AssetBrowser's own scan/import code is hardcoded to that name - changing
		//that would touch every existing project, which is a different and much
		//larger change than what was asked for here.
		static void ScaffoldNewLevel(const std::string& level_path)
		{
			fs::path level_file(level_path);
			fs::path assets_dir = level_file.parent_path() / "Assets";
			fs::create_directories(assets_dir);

			//A brand new level starts with no material files, and World::CreateMaterial
			//refuses to create anything outside one it already knows about - so with
			//nothing here the Materials panel's Create Material dialog has nowhere to
			//put a new material and dead-ends. One is scaffolded up front, named after
			//the level and living in its Assets folder (the "path" declared below), so
			//there is always somewhere to create into without hand-editing the level's
			//JSON first.
			std::string mat_name = level_file.stem().string() + ".mat";
			json mat_file;
			mat_file["root"] = "";
			mat_file["materials"] = json::array();
			std::ofstream mat_out((assets_dir / mat_name).string());
			mat_out << mat_file.dump(4);

			//Resolved against the process's working directory when relative, so an
			//absolute path here is what makes a level scaffolded on any drive, in any
			//folder, still resolve its own assets correctly.
			json level;
			level["world"]["path"] = assets_dir.string() + "\\";
			level["world"]["lights"] = json::array({
				{ {"type", "ambient"}, {"name", "ambient"}, {"color_up", "050050050"}, {"color_down", "020020020"} }
			});
			level["world"]["entities"] = json::array();
			level["world"]["instances"] = json::array();
			level["world"]["templates"] = json::array();
			level["world"]["material_files"] = json::array({ mat_name });

			std::ofstream level_out(level_path);
			level_out << level.dump(4);
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

		void NewLevelWithDialog(EditorState& state, SceneEditorApp& app)
		{
			std::string level_path = SaveLevelFileDialog(app.wnd, "Create New Level", "level.json");
			if (!level_path.empty()) {
				ScaffoldNewLevel(level_path);
				//No config.json is written, so this resolves to the level's own
				//folder - exactly right for a level that owns its own Assets folder.
				state.project_root = DeriveProjectRoot(level_path);
				app.RequestOpenLevel(level_path);
			}
		}

		void SaveLevelAsWithDialog(EditorState& state, SceneEditorApp& app)
		{
			if (state.current_level_path.empty()) {
				state.status_message = "No level open, nothing to save.";
				return;
			}
			std::string default_name = fs::path(state.current_level_path).filename().string();
			std::string new_path = SaveLevelFileDialog(app.wnd, "Save Level As", default_name);
			if (new_path.empty()) {
				return;
			}
			std::error_code ec;
			if (!fs::equivalent(new_path, state.current_level_path, ec)) {
				//Seeds the new file with the current one's on-disk content - including
				//whatever a game's own tooling put in it that this editor does not
				//understand - so the save below (which reads-then-merges rather than
				//writing from scratch, see SceneSerializer::Save) has something correct
				//to merge the live scene into, exactly as if the level had always lived
				//at the new path.
				fs::copy_file(state.current_level_path, new_path, fs::copy_options::overwrite_existing, ec);
				if (ec) {
					state.status_message = "Save As failed: could not write " + new_path + ": " + ec.message();
					return;
				}
			}
			//Deliberately leaves project_root and every asset reference alone: the
			//level file moved, not the project. A level saved into a different
			//project's folder still resolves its models/materials from where it
			//always has, the same way "Save As" on a document does not also relocate
			//the images it links to.
			state.current_level_path = new_path;
			//Same snapshot File/Save Level takes: the DOF effect's live state lives on
			//the app, not EditorState, so it has no other way into what Save writes.
			state.render_settings = RenderSettings::ToJson(app);
			SceneSerializer::Save(state);
		}

	}
}
