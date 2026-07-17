#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// File-based remote control for scripted/agent-driven testing, enabled with the
	// --automation <dir> command-line switch. A driver process drops a command.txt
	// (one command per line, double-quote arguments containing spaces) into <dir>;
	// the editor executes the commands on its main thread and answers by atomically
	// writing <dir>/response.txt at the end of the same frame. See
	// Tools/SceneEditor/automation/README.md for the command reference and a
	// PowerShell driver.
	namespace EditorAutomation {
		// Enables the channel rooted at `dir` (created if missing).
		void Init(const std::string& dir);
		bool Enabled();

		// Polls <dir>/command.txt, consumes and executes it if present, buffering the
		// per-command responses. Runs once per frame on the main thread, before the
		// frame renders, so state mutations are visible in the frame the response
		// (and any screenshot) describes.
		void ProcessCommands(EditorState& state, SceneEditorApp& app);

		// Completes deferred work that needs the finished frame (screenshot capture)
		// and flushes buffered responses to <dir>/response.txt. Called from
		// SceneEditorApp::Present after the UI is rendered into the backbuffer but
		// before it is presented.
		void OnFrameEnd(EditorState& state, SceneEditorApp& app);
	}
}
