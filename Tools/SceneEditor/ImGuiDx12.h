#pragma once

// Dear ImGui on the engine's Direct3D 12 context.
//
// The stock backend (imgui_impl_dx12) wants a descriptor heap of its own and a command list it can
// record into. Both exist here - GfxContext::BeginExternal hands the frame's list over and takes it back -
// and what is left for this file is the texture ids: ImGui draws a texture by the GPU handle of a
// descriptor in the heap that is bound while it draws, so every texture the editor shows (material
// previews, thumbnails, the model preview) is copied into this heap when the UI asks for its id, and
// its resource is moved to the shader-read state just before ImGui is rendered.

#include <imgui.h>
#include <Core/DXCore.h>

namespace ImGuiDx12 {
	// After the engine's InitDirectX. False if the heap or the backend could not be created.
	bool Init(HotBite::Engine::Core::DXCore* dx);
	void Shutdown();
	// Once per frame, before ImGui::NewFrame.
	void NewFrame();
	// Draws `draw_data` into the engine's render target (the back buffer).
	void Render(ImDrawData* draw_data);
	// The id to give ImGui::Image / ImageButton for a texture, valid until the end of the frame it was
	// asked in. Null when `srv` is null or the frame has used all its descriptors.
	ImTextureID TextureId(GfxSRV* srv);
}
