#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The "Log" panel: a live view of Core::Log's in-memory ring buffer, docked as a
	// full-width strip at the bottom of the editor (EditorLayout::BeginDockspace),
	// the way an IDE's console/output pane sits. Toggled from View/Log like every
	// other panel (SceneEditor.cpp); nothing here owns its own visibility.
	namespace LogPanel {
		void Draw(EditorState& state);
	}
}
