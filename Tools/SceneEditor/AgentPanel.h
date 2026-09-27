#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The Claude panel (View/Claude): a chat with an agent that can see and edit the
	// open level. The process, the transcript and the undo grouping live in
	// ClaudeAgent.h; this is only the window.
	//
	// It is scriptable like the rest of the editor - agent_send / agent_status /
	// agent_transcript / agent_stop / agent_new (EditorAutomation.cpp) - which is how
	// the automation suite drives it against a stand-in for Claude Code.
	namespace AgentPanel {
		void Draw(EditorState& state);
	}
}
