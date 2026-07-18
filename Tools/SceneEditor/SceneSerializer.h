#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace SceneSerializer {
		// Re-reads the currently open level.json from disk, merges in any Transform
		// overrides made to FBX-authored entities and the session's placed instances,
		// and writes it back out. Everything else in the file (lights, materials,
		// audio, pre-existing templates) passes through untouched.
		void Save(EditorState& state);

		// Reads the editor-only data (entity groups shown in the Entities panel)
		// stored under the level JSON's top-level "editor" object, which the engine's
		// World::Load ignores. Called on level open; missing/malformed data just
		// leaves the state empty.
		void LoadEditorData(EditorState& state, const std::string& level_json_path);
	}
}
