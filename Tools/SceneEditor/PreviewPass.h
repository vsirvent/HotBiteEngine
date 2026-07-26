#pragma once

// Not <d3d11.h> directly: it reaches Windows.h (and so winsock.h) on its own, and
// every engine header that follows then pulls winsock2.h into a translation unit
// that already has the older one. DXCore.h is the engine's D3D entry point and
// establishes that order, which is why it goes first here.
#include <Core/DXCore.h>
#include <Core/SimpleShader.h>

#include "imgui.h"

namespace HotBiteEditor {

	// Offscreen rendering for the editor's asset previews - the material thumbnails
	// (MaterialPreview.h) and the template model viewport (ModelPreview.h).
	//
	// == Why this is not the engine's renderer ==
	// A preview is a second view of the world, and the obvious way to draw one would
	// be to ask RenderSystem for it. That is deliberately not what happens. The
	// RenderSystem owns some forty backbuffer-sized render textures (the deferred
	// light maps, GI, ReSTIR and ray-tracing targets, bloom, motion, DOF, autofocus),
	// a background ray-tracing thread and draw trees bound to one coordinator, all of
	// it sized and scheduled for exactly one view per frame. Rendering a second view
	// through it means hoisting every one of those into a per-view context - a
	// renderer rewrite, and a large one, in exchange for a thumbnail.
	//
	// So a preview is its own small forward pass instead: bind an offscreen target,
	// draw a handful of meshes with a dedicated shader pair, put back whatever was
	// bound. This file is the part both previews share.
	//
	// == The one rule ==
	// Everything here runs on the render thread, *inside* the editor's own frame -
	// a panel calls it while building its UI, between the scene draw and
	// ImGui::Render(). That is why ScopedState exists: a preview draw walks over the
	// render target, the viewport and the pixel-stage resource bindings that the
	// frame around it is in the middle of using, and every one of them has to be put
	// back before the panel keeps drawing.
	namespace PreviewPass {

		// The three-light studio rig MaterialPreviewPS shades with, as directions
		// pointing from the surface *towards* each light, in world space.
		//
		// The rig is built around the camera, not fixed in the world: the key sits in
		// front of the viewer, up and to the right, the fill opposite it on the left,
		// and the back light behind the subject to pick out its silhouette. Anchoring
		// it to the camera is what makes the model viewport's orbit legible - with the
		// rig fixed in world space, turning the model past a quarter of a turn put the
		// key behind it and left the face you were looking at in shadow.
		//
		// It is one rig for both previews so that the invariant the two were built on
		// holds: a material looks the same in its swatch as it does on the model.
		struct LightRig {
			HotBite::Engine::float3 key;
			HotBite::Engine::float3 fill;
			HotBite::Engine::float3 back;
		};

		// The rig for a camera at `eye` looking at `target`. A degenerate camera (no
		// distance to the target, or looking straight down the world up axis) still
		// produces a usable rig rather than NaNs.
		LightRig MakeLightRig(const HotBite::Engine::float3& eye,
			const HotBite::Engine::float3& target);
		// Uploads it into MaterialPreviewPS's constant buffer. Call before
		// CopyAllBufferData, like every other constant.
		void BindLightRig(HotBite::Engine::Core::SimplePixelShader* ps, const LightRig& rig);

		// An offscreen colour + depth surface a preview draws into and then hands to
		// ImGui as a texture. ImGui only reads it when the frame is submitted at the
		// end, so a Target must outlive the panel code that drew into it - which is
		// why both previews keep theirs in a cache rather than on the stack.
		struct Target {
			ID3D11Texture2D* texture = nullptr;
			ID3D11RenderTargetView* rtv = nullptr;
			ID3D11ShaderResourceView* srv = nullptr;
			ID3D11Texture2D* depth_texture = nullptr;
			ID3D11DepthStencilView* dsv = nullptr;
			int width = 0;
			int height = 0;

			// Creates the surface, or recreates it at the new size. False leaves the
			// Target released and unusable (and is the caller's cue to draw nothing).
			bool Ensure(int width, int height);
			// Binds this target and a viewport covering it, then clears both planes.
			// Only valid inside a ScopedState, which is what puts the previous
			// bindings back.
			void Bind(const float clear_color[4]) const;
			void Release();

			ImTextureID Handle() const { return (ImTextureID)srv; }
			bool Valid() const { return srv != nullptr; }
		};

		// Captures the pipeline bindings a preview draw overwrites and restores them
		// on scope exit: the render target, the depth target and the viewport.
		//
		// It also unbinds the pixel-stage shader resources on the way out, because a
		// preview binds material textures there and the same textures can be bound as
		// render *outputs* elsewhere in the frame - leaving them on the pixel stage
		// trips D3D's read/write hazard detection and the runtime silently drops one
		// of the two bindings.
		//
		// The tessellation and geometry stages are switched off rather than saved:
		// a preview mesh is never tessellated, so whatever the scene left bound would
		// otherwise still run over it. Nothing restores them because the next frame's
		// scene draw sets its own, exactly as it does for every other stage it owns.
		class ScopedState {
		public:
			ScopedState();
			~ScopedState();
			ScopedState(const ScopedState&) = delete;
			ScopedState& operator=(const ScopedState&) = delete;

		private:
			ID3D11RenderTargetView* prev_rtv = nullptr;
			ID3D11DepthStencilView* prev_dsv = nullptr;
			D3D11_VIEWPORT prev_viewport{};
			UINT prev_viewport_count = 1;
		};
	}
}
