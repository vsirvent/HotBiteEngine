#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace ProjectBrowser {
		void Draw(EditorState& state, SceneEditorApp& app);

		// The File menu actions: pick a level.json / scaffold a new project via the
		// native dialogs, then open the result. No-ops when the dialog is cancelled.
		void OpenLevelWithDialog(EditorState& state, SceneEditorApp& app);
		void NewProjectWithDialog(EditorState& state, SceneEditorApp& app);

		// Walks up from a level.json path looking for the config.json that marks the
		// project root; falls back to the level's own directory. Shared by the "Open
		// Level..." menu item and the automation open_level command.
		std::string DeriveProjectRoot(const std::string& level_json_path);
	}
}
