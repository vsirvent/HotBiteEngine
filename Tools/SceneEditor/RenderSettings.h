#pragma once

#include "SceneEditor.h"

#include <Core/Json.h>

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

		// Banner naming the buffer currently being shown in place of the frame, and
		// which denoisers are bypassed. Drawn every frame while a debug buffer is
		// selected: several of the buffers are legitimately black or near-black over
		// most of the screen, and without something on top saying so, an empty
		// buffer and a broken editor look identical.
		void DrawOverlay(SceneEditorApp& app);

		// Programmatic access, shared with the automation channel's `render` command.
		// Keys: rt_quality (off|low|mid|high), rt_reflections, rt_refractions,
		// rt_indirect, aa, motion_blur, motion_blur_scale, dof, dof_autofocus, dof_focus, dof_amplitude,
		// lens_flare, wireframe (booleans take 0/1, motion_blur_scale/dof_focus/dof_amplitude take
		// floats; setting either of those switches dof_autofocus off, since
		// autofocus drives both focus and amplitude).
		//
		// Buffer debugging: debug_buffer (off|scene|light|bloom|emission|reflection|
		// refraction|indirect|volumetric|dust|lens_flare|depth|position|normal|motion|
		// gi_cache|gi_cache_conf|ray_sources|ray_dispersion|ray_reflex|ray_density|
		// ray_opacity - the authoritative list is RenderSystem::DebugBufferName),
		// debug_gain (float, exposure for the HDR colour buffers and the scale for the
		// motion and ray scalar ramps), and gi_denoise / rt_denoise (0 bypasses that
		// denoiser, so its buffer carries the raw traced signal).
		bool Set(SceneEditorApp& app, const std::string& key, const std::string& value, std::string& error);

		// One-line JSON of the current settings (for the automation channel).
		std::string Dump(SceneEditorApp& app);

		// The subset of Dump() worth carrying between sessions: every persistent
		// toggle/slider, but never the four debug-only keys (debug_buffer,
		// debug_gain, gi_denoise, rt_denoise) - a buffer view or a bypassed
		// denoiser carried into a freshly opened level would look like the level
		// rendering wrong, exactly why ApplyHighDefaults resets them. Read by
		// SceneSerializer::Save (as the level's "editor"/"render" block); empty
		// when no level is loaded.
		nlohmann::json ToJson(SceneEditorApp& app);

		// The inverse of ToJson: applies every field present in `j`, leaving
		// anything absent at whatever ApplyHighDefaults already set. Call it right
		// after ApplyHighDefaults on level load, once the post-process pipeline
		// (and so the DOF effect dof_focus/dof_amplitude read from) exists.
		void ApplyFromJson(SceneEditorApp& app, const nlohmann::json& j);
	}
}
