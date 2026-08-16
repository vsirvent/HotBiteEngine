#pragma once

#include "SceneEditor.h"

#include <string>
#include <vector>

namespace HotBiteEditor {

	// Recompiles the engine's shaders from their .hlsl sources while the editor is
	// running, so a shader edit can be seen without restarting and reloading a level
	// (Core::ShaderCompiler does the compiling, ISimpleShader the in-place swap).
	//
	// **The compile runs on a worker thread and only the swap happens on the render
	// tick**, and that split is not an optimisation. fxc takes 36 seconds on
	// GIRayTraceCS and a couple of seconds on a mid-sized pixel shader; compiling on
	// the thread that pumps messages made Windows declare the editor hung and kill
	// it (`Application Hang`/AppHangB1 in the event log) partway through the first
	// full reload. So a reload is asynchronous: a request queues jobs, the worker
	// compiles them one at a time, and Tick applies whatever finished - which is
	// milliseconds of device work, safe to do between frames.
	//
	// Consequences worth knowing:
	//
	// - A caller gets no result back. Progress is in the status bar, failures in the
	//   log, and the automation channel answers "queued" plus a `shader_reload_status`
	//   command to poll. Anything scripted has to wait for idle.
	// - Jobs carry the resolved source path and profile as *strings*, never the
	//   shader pointer: the worker outlives an orderly shutdown (a compile cannot be
	//   interrupted, so Shutdown detaches it) and must not touch anything the editor
	//   is tearing down.
	// - A failed compile is a non-event. ISimpleShader::ApplyCompiled is never
	//   reached without bytecode, so the frame keeps drawing the previous shader and
	//   the compiler's message goes to the log.
	namespace ShaderReload {

		// Registers the shader source folders (see ShaderCompiler::AddDefaultSourceFolders).
		// Called once from the editor's constructor.
		void Init();

		// Stops accepting work and lets go of the worker. Called from the editor's
		// destructor.
		void Shutdown();

		// Queues one shader by the name the engine loaded it under
		// ("MainRenderPS.cso", or the bare stem). False with `error` set if it is not
		// loaded or has no source - a compile failure comes later and is not reported
		// here.
		bool ReloadOne(EditorState& state, const std::string& name, std::string& error);

		// Queues every loaded shader, or only those whose source or included headers
		// were written since they last loaded. Returns how many were queued.
		int ReloadAll(EditorState& state, bool only_changed);

		// Watches for edits and queues what changed, off by default. Polled from the
		// render tick; the poll is a timestamp check per dependency file, so it costs
		// nothing until something is written.
		bool AutoEnabled();
		void SetAuto(bool enabled);

		// Per-tick hook: adopts a newly opened project's own shader folder, applies
		// compiles that finished, and (when auto reload is on) queues what changed.
		void Tick(EditorState& state);

		// Jobs still queued or in flight. 0 means every request has been applied.
		int Pending();

		// The last finished batch: "<n> reloaded, <n> failed". Empty until one
		// finishes.
		const std::string& LastReport();

		// Compiler messages from the last finished batch, one per failed shader.
		const std::vector<std::string>& LastErrors();

		// "<n> sources" plus one line per registered folder.
		std::string SourcesReport();

		// Adds/removes a folder of .hlsl sources at run time. A folder added later
		// wins a name collision, which is how a copy of an engine shader can be tried
		// out without touching the tree.
		bool AddSourceFolder(const std::string& folder, std::string& error);
		bool RemoveSourceFolder(const std::string& folder, std::string& error);
	}
}
