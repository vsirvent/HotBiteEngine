#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace SceneSerializer {
		// Re-reads the currently open level.json from disk, merges in any Transform
		// overrides made to FBX-authored entities and the session's placed instances,
		// and writes it back out. Everything else in the file (lights, materials,
		// audio, pre-existing templates) passes through untouched.
		void Save(EditorState& state);
	}
}
