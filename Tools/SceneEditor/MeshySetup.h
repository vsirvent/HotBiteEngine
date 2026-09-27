#pragma once

#include "SceneEditor.h"

#include <string>

namespace HotBiteEditor {
	// Sets the Meshy API key the hotbite-editor MCP server's meshy_* tools read
	// (%TEMP%\HotBiteMeshy\config.json) from inside the editor, so a person never
	// has to open PowerShell by hand - the "Claude/Set Meshy API Key..." menu item
	// and the button beside it in the Claude panel (AgentPanel.h) both call
	// RequestShow().
	//
	// This does not duplicate setup-meshy-key.ps1's own logic (where the file
	// goes, what the optional caps mean): it runs that same script in the
	// background, over a pipe, and writes the typed key to its stdin - the script
	// is the one place that knows the file format, so a change there needs no
	// matching change here. The key still never goes near the agent or a chat
	// transcript; it goes from this popup's password field straight to the
	// script's own process and nowhere else. See CLAUDE.md's note on secrets.
	namespace MeshySetup {
		// Opens the popup on the next Draw. An ImGui popup cannot be opened from
		// inside the menu command callback that requests it (see SceneEditor.cpp's
		// DrawDeleteRequest), so this only sets a flag Draw consumes.
		void RequestShow();

		// Draws the popup when requested or already open. Call once per frame at
		// window scope, e.g. beside SceneEditorApp::DrawGridSettingsPopup().
		void Draw(EditorState& state);

		// True while the background script is running - Draw() already disables
		// its own inputs on this, exposed for anything else that wants to know.
		bool Busy();

		// True while the popup is open. Only meaningful after Draw() has run for
		// the current frame.
		bool IsOpen();

		// The outcome of the last save, if any - for the automation command this
		// backs (meshy_setup_status) and nothing else. `message` never contains the
		// key: RunScript only ever puts the script's own stdout/exit status there.
		struct Result {
			bool has_result = false;
			bool ok = false;
			std::string message;
		};
		Result LastResult();

		// Joins the background thread, if one is still running. Called before the
		// editor exits.
		void Shutdown();
	}
}
