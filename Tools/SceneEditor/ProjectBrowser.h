#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The File menu's project/level lifecycle actions. There is no project *panel*:
	// before a level is open the editor shows just its menu bar, and everything the
	// old panel displayed (project root, level path) is either in the title bar or
	// not worth a window of its own.
	namespace ProjectBrowser {
		// Pick a level .json / scaffold a new project via the native dialogs, then
		// open the result. No-ops when the dialog is cancelled.
		void OpenLevelWithDialog(EditorState& state, SceneEditorApp& app);
		void NewProjectWithDialog(EditorState& state, SceneEditorApp& app);

		// Walks up from a level path looking for the config.json that marks the
		// project root; falls back to the level's own directory. Shared by the "Open
		// Level..." menu item and the automation open_level command.
		std::string DeriveProjectRoot(const std::string& level_json_path);
	}
}
