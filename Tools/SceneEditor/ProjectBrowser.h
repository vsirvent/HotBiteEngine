#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace ProjectBrowser {
		void Draw(EditorState& state, SceneEditorApp& app);

		// Walks up from a level.json path looking for the config.json that marks the
		// project root; falls back to the level's own directory. Shared by the "Open
		// Level..." button and the automation open_level command.
		std::string DeriveProjectRoot(const std::string& level_json_path);
	}
}
