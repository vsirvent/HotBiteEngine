#pragma once

#include "SceneEditor.h"

#include <string>
#include <vector>

namespace HotBiteEditor {

	// Claude inside the Scene Editor: the process behind the Claude panel (AgentPanel.h).
	//
	// It is Claude Code run headless as a child process (`claude -p` with stream-json
	// on stdin and stdout), logged in however the user's Claude Code already is, so
	// there is no API key to configure. What makes it an *editor* agent is its MCP
	// config: the hotbite-editor server (Tools/SceneEditor/mcp), which reaches this
	// editor through the same file-based automation channel the regression suites
	// drive. Its tools read the level and the assets, take screenshots, and run any
	// automation command (`editor_command`) - so whatever a test can do to the editor,
	// the agent can.
	//
	// Three things decide how that is wired, and none of them is guessable:
	//  - The agent gets a channel root of its own (EditorAutomation::AddRoot), never
	//    the --automation one. The channel has room for one driver per folder; a test
	//    suite or a Claude Code session driving the editor through --automation would
	//    otherwise overwrite the agent's command.txt, and the agent theirs.
	//  - Each turn - from Send until Claude's `result` - is one undo group
	//    (EditorHistory::BeginGroup), so everything the agent did for one message
	//    comes back with one Ctrl+Z. The agent's edits arrive as automation commands
	//    executed on this thread between frames, so they land inside the group without
	//    any cooperation from the agent side.
	//  - The agent may not write files. Claude Code's own Write/Edit/Bash are not in
	//    its allow list (and headless mode denies what is not allowed): the editor
	//    holds the live copy of the level, its .mat and .tpl files, and an edit made on
	//    disk behind its back is overwritten by the next save - or worse, half-loaded.
	//    Changes go through editor_command, which is undoable and marks things dirty.
	//
	// Threads: one reader per pipe (stdout events, stderr diagnostics). Events are
	// parsed there and queued; Tick() applies them to the transcript on the main
	// thread, which is the only thread the transcript and the undo history are touched
	// from.
	namespace ClaudeAgent {

		enum class Status {
			Idle,      // no process; the next Send starts one
			Busy,      // a turn is running
			Ready,     // process up, waiting for the next message
			Stopping,  // interrupt sent, waiting for the turn to end
			Failed,    // setup problem, or the process died - see LastError()
		};

		struct Item {
			enum class Kind { User, Assistant, Tool, Info, Error, Result };
			Kind kind = Kind::Info;
			std::string text;        // message text; for Tool the input as JSON
			// Tool calls only.
			std::string tool_id;
			std::string tool_name;   // with the mcp__<server>__ prefix removed
			std::string tool_result;
			bool tool_done = false;
			bool tool_error = false;
			int tool_images = 0;
		};

		// Where everything the agent needs was found, or why it was not.
		struct Setup {
			std::string claude;      // claude.exe (or a .js run through node - see ClaudeAgent.cpp)
			std::string node;        // node.exe, which runs the MCP server
			std::string server;      // Tools/SceneEditor/mcp/server.js
			std::string base_dir;    // where the agent's files live (config, prompt, its channel)
			std::string channel_dir; // the agent's automation channel root
			std::string problem;     // non-empty when something is missing
		};
		Setup Locate(EditorState& state);
		// Uses `path` as Claude Code instead of searching for it ("" = search again).
		// A path ending in .js is run through node, which is how the automation suite
		// puts a scripted stand-in behind the panel. Takes effect at the next start.
		void SetClaudeOverride(const std::string& path);

		// Starts a turn: launches the process first when there is none (resuming the
		// conversation when there was one), opens the turn's undo group and writes the
		// message. False with `error` when busy or when setup fails.
		bool Send(EditorState& state, const std::string& text, std::string& error);
		// Asks Claude to stop the running turn; kills the process if it does not
		// within a few seconds (the next Send resumes the conversation).
		void Stop();
		// Ends the process and forgets the conversation and the transcript.
		void NewConversation();
		// Main thread, once per frame: applies what the reader threads queued.
		void Tick(EditorState& state);
		// Kills the process. Called before the editor goes away.
		void Shutdown();

		Status GetStatus();
		const char* StatusName(Status s);
		std::string LastError();
		std::string SessionId();
		std::string ActiveModel();   // what the process reported at startup
		double TotalCostUsd();       // summed over the turns of this conversation
		// "" = Claude Code's default. Takes effect when the next process starts; a
		// running one is restarted (resuming the conversation) on the next Send.
		// `remember` stores the choice per user (%APPDATA%\HotBite\claude-panel.json),
		// so the model picked in the panel is the one every later session starts on -
		// otherwise each launch would silently fall back to the most expensive default.
		// The automation command does not remember: a test must not change what the
		// person's next session costs.
		std::string Model();
		void SetModel(const std::string& model, bool remember = false);

		// Main thread only.
		const std::vector<Item>& Transcript();
		std::string LiveText();      // the assistant text streaming in right now
		// Bumped whenever the transcript or the live text changes (auto-scroll).
		uint64_t Revision();
	}
}
