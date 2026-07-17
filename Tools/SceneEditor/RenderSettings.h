#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The editor's render feature control, mirroring how Marbles drives the engine
	// (RenderSystem Set*/Get* plus the DOF stage of the post-process chain). All
	// three entry points operate on the same live engine state, so the "Render"
	// menu, the automation channel and the defaults can never disagree.
	namespace RenderSettings {
		// Everything on, ray tracing at HIGH. Called right after the post-process
		// pipeline is installed on level load.
		void ApplyHighDefaults(SceneEditorApp& app);

		// Draws the "Render" menu of the main menu bar (enabled once a level is
		// loaded). Widgets read from and write to the RenderSystem directly.
		void DrawMenu(SceneEditorApp& app);

		// Programmatic access, shared with the automation channel's `render` command.
		// Keys: rt_quality (off|low|mid|high), rt_reflections, rt_refractions,
		// rt_indirect, aa, motion_blur, dof, dof_focus, dof_amplitude, lens_flare,
		// wireframe (booleans take 0/1, dof_focus/dof_amplitude take floats).
		bool Set(SceneEditorApp& app, const std::string& key, const std::string& value, std::string& error);

		// One-line JSON of the current settings (for the automation channel).
		std::string Dump(SceneEditorApp& app);
	}
}
