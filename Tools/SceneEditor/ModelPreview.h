#pragma once

#include "SceneEditor.h"

#include "imgui.h"

#include <string>

namespace HotBiteEditor {

	// The template model viewport in the Templates panel: the actual mesh, with its
	// actual material and animation, rendered offscreen and shown as an ImGui image
	// the user can orbit.
	//
	// This is the "what am I editing?" answer the panel was missing. A template is a
	// component block - a mesh name, a material name, an animation name - and until
	// the object is placed in the scene there is nothing that says whether those
	// three name the right things.
	//
	// == What it draws ==
	// Every renderable part of the template, read live from the template entities in
	// the templates coordinator, composed through each part's own base transform.
	// That means the preview shows what SpawnInstance will produce, including the
	// template's own scale and rotation - the thing an instance record composes on
	// top of (World::GetTemplateBaseTransform). Both kinds of template work: an
	// authored one is a single entity, an imported .fbx is one per node.
	//
	// It is the preview *pass*, not the engine's render path (see PreviewPass.h for
	// why, and MaterialPreviewPS.hlsl for what the shading models): a studio rig
	// carried on the camera, no shadows, no GI, no post-process. Its job is to tell
	// you that the mesh and the material are the ones you meant, not to predict the
	// final frame.
	//
	// == Animation ==
	// The preview drives its own copy of the template's Components::Mesh rather than
	// the template entity's, so playing an animation here cannot leave animation
	// state on the thing SpawnInstance clones. The clock is the preview's own, so
	// pausing freezes the pose without touching the scene, which keeps running.
	//
	// Like everything in the preview pass, Draw() must be called on the render thread
	// from inside the editor's frame, while a panel is building its UI.
	namespace ModelPreview {

		// The viewport's camera and playback state. Owned by the panel - one per
		// preview widget - so the preview itself holds no UI state and two panels
		// showing the same template can look at it from different angles.
		struct View {
			float yaw = 0.7f;    // radians, orbit around the model's up axis
			float pitch = 0.35f; // radians above the horizon, clamped near the poles
			float zoom = 1.0f;   // multiple of the distance that frames the model
			bool play = true;    // animate, when the template has an animation
			// An animation from the template's library to play *instead* of the one the
			// template selects, by its logical name ("walk"). This is how the Animations
			// section auditions a clip: seeing what a clip looks like on this model must
			// not be an edit, so the override lives in the viewer's state and nothing is
			// written back. Empty means "whatever the template plays".
			std::string clip_override;
		};

		// Draws the preview as an ImGui item `size` pixels across, handling its own
		// input: left-drag orbits, wheel zooms, double-click reframes. False (having
		// drawn a placeholder) when there is nothing to show - an unknown template, a
		// template with no mesh, or a device/shader that is not up - which the caller
		// can use to explain why.
		bool Draw(EditorState& state, const std::string& template_name, View& view,
			const ImVec2& size);

		// Releases every GPU resource. Editor shutdown and level close, exactly like
		// MaterialPreview::Shutdown - the cached targets are D3D textures ImGui still
		// holds ids for, so this has to run before the ImGui backend goes away.
		void Shutdown();
	}
}
