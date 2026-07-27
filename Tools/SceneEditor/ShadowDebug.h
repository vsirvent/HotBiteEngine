#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// The two shadow debug views, plus a legend naming their colours.
	//
	// Directional shadows are split in two: the cascades carry only the casters that
	// *move* and are re-rendered every frame, while everything marked static goes into
	// one plain map fitted to the widest cascade and re-rendered only rarely. Each half
	// has its own failure mode, so each gets its own view - `Cascades` colours a pixel
	// by the slice that shaded it, `StaticMap` by whether the static map reaches it at
	// all and whether a static caster occludes it.
	//
	// Cascaded shadows are the one part of the renderer whose output tells you almost
	// nothing about whether it is set up well. A blurry shadow could mean the cascade
	// covering it is too big, or that the shadow distance reaches past where the
	// cascades were meant to end, or that the split blend has collapsed cascade 0 into
	// a sliver nothing is standing in - and all three look identical on screen. Tinting
	// by cascade makes the boundaries and their coverage directly visible, and the
	// legend puts the number that matters next to each band: shadow texels per world
	// unit, which is what the whole fit exists to maximise.
	//
	// The tinting itself is the engine's, not the editor's: it happens per pixel inside
	// the lighting shaders, switched by DIR_LIGHT_FLAG_DEBUG_CASCADES on the light
	// (Components/Lights.h). A viewport overlay could not do this - the cascade a pixel
	// falls in is only known where the shadow lookup happens, and the cascade volumes
	// are always wrapped around the viewer, so drawing them as wireframes shows you the
	// inside of a box you are standing in. This file is the switch and the legend.
	namespace ShadowDebug {

		// Applies EditorState::shadow_debug_view to every directional light and draws the
		// matching legend. Called every frame: it also *clears* the flags, so a light can
		// never be left tinting after the view is switched off.
		void Draw(EditorState& state);

		// Cascades the legend has a colour for, matching MAX_SHADOW_CASCADES.
		constexpr int MAX_DRAWN_CASCADES = 4;
	}
}
