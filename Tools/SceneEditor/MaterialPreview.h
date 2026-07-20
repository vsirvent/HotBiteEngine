#pragma once

#include "SceneEditor.h"

#include <Core/Material.h>
#include "imgui.h"

namespace HotBiteEditor {
	// Material thumbnails for the Materials panel: one lit sphere per material,
	// rendered offscreen and handed to ImGui as a texture.
	//
	// Each material gets a cached render target that is drawn once and then reused
	// every frame; a thumbnail is only re-rendered when Invalidate() marks it dirty.
	// That matters because the panel shows the whole material list at once - without
	// the cache, opening it would cost one draw per material per frame.
	//
	// The pass is deliberately not the engine's render path (see
	// MaterialPreviewPS.hlsl): it shows the material's own maps and constants under
	// a fixed studio rig, with no shadows, GI, parallax or tessellation. It is a way
	// to tell materials apart at a glance, not a preview of the final look.
	//
	// Everything here must be called from the render thread, between the scene draw
	// and ImGui::Render(). Render() binds its own render target and restores whatever
	// was bound before, so panels can call it while building their UI.
	namespace MaterialPreview {

		// The thumbnail for `material`, rendering it first if it is missing or dirty.
		// Returns null when the material is null or the device is not up, in which
		// case the caller should fall back to a plain colour swatch.
		ImTextureID Get(HotBite::Engine::Core::MaterialData* material, int size);

		// Marks a material's thumbnail stale, so the next Get() redraws it. Call this
		// from every edit that changes how the material looks - the cache cannot see
		// property writes, so a missed call leaves a thumbnail showing the old values.
		void Invalidate(const std::string& material_name);

		// Drops every cached thumbnail (level close, or a bulk material reload).
		void InvalidateAll();

		// Releases all GPU resources, including the shared sphere and sampler.
		// Called on editor shutdown and on level close.
		void Shutdown();
	}
}
