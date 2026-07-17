#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace AssetBrowser {
		void Draw(EditorState& state);

		// Scans the project's Assets/Objects folder for .fbx templates and loads any
		// not-yet-loaded ones into the World. Draw calls this lazily on project change;
		// automation calls it before template commands so both see the same list.
		void EnsureTemplatesScanned(EditorState& state);

		// The File/Import Object... menu action: picks an .fbx via the native file
		// dialog and imports it. No-op when the dialog is cancelled.
		void ImportObjectWithDialog(EditorState& state);

		// Programmatic equivalent of Import Object, minus the file dialog: copies
		// the .fbx into Assets/Objects, loads it as a template and selects it.
		// Returns false with `error` set on failure.
		bool ImportObject(EditorState& state, const std::string& fbx_path, std::string& error);

		// Programmatic equivalent of the "Place at Origin" button: spawns an instance
		// of `template_name` at the origin, records it for save and selects it.
		// Returns false with `error` set on failure.
		bool PlaceTemplate(EditorState& state, const std::string& template_name, std::string& error);
	}
}
