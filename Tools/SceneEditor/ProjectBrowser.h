#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The File menu's project/level lifecycle actions. There is no project *panel*:
	// before a level is open the editor shows just its menu bar, and everything the
	// old panel displayed (project root, level path) is either in the title bar or
	// not worth a window of its own.
	//
	// "New Level" and "Save As" both put the choice of *where* entirely in the
	// user's hands (a native Save dialog), rather than dictating a folder layout:
	// the only thing the editor actually needs is an "Assets" folder next to the
	// level for imported models to land in (see AssetBrowser's Import Model /
	// folder scan), and that is created lazily, on the first import, not scaffolded
	// up front. There is no forced subfolder nesting and no config.json written for
	// a level created this way - DeriveProjectRoot's existing fallback (the level's
	// own directory) is already the right project root for a single-level project.
	namespace ProjectBrowser {
		// Pick a level .json via the native "Open" dialog, then open it. A no-op
		// when the dialog is cancelled.
		void OpenLevelWithDialog(EditorState& state, SceneEditorApp& app);

		// Pick where a brand-new level's .json goes via the native "Save" dialog,
		// write a minimal starting level there (ambient light, no geometry), and
		// open it. A no-op when the dialog is cancelled.
		void NewLevelWithDialog(EditorState& state, SceneEditorApp& app);

		// Pick a new path for the *currently open* level via the native "Save"
		// dialog, copy the level's current file there, point the session at it,
		// and save immediately - so what ends up on disk at the new path reflects
		// the live scene, not just whatever the old file last had. Assets keep
		// resolving against the original project (only the level file moves), the
		// same way "Save As" leaves a document's linked resources where they are.
		// A no-op when the dialog is cancelled; requires a level to already be open.
		void SaveLevelAsWithDialog(EditorState& state, SceneEditorApp& app);

		// Walks up from a level path looking for a config.json that marks the
		// project root (an older, multi-level-project convention - see
		// Tests/DemoGame and Marbles); falls back to the level's own directory,
		// which is exactly right for a level created via NewLevelWithDialog. Shared
		// by "Open Level...", "New Level...", and the automation open_level command.
		std::string DeriveProjectRoot(const std::string& level_json_path);
	}
}
