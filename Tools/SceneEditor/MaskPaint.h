#pragma once

#include "SceneEditor.h"

#include <d3d11.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// A brush that paints a multi-material layer's mask image, so a splat map for a
	// terrain (or anything else wearing a multi-material) can be authored without
	// leaving the editor or round-tripping through an external paint program.
	//
	// A painting session edits one layer's mask channel of one image at a time - a
	// single RGBA file can still back four layers (see MULTITEXT_MASK_CHANNEL_* in
	// Core/Material.h), and a session only ever touches the channel the layer it was
	// started on reads. While a session is open, the edited image is bound as that
	// layer's mask through Core::MultiMaterialData::SetLiveMask, so every stroke is
	// visible in the viewport immediately - Commit is what writes it to disk and
	// Cancel is what throws the whole session away.
	//
	// Deliberately outside EditorHistory (see the "out of scope by design" list at
	// the top of EditorHistory.h): a stroke is a pixel edit to an image file, and like
	// File/Import Object, undoing it would mean restoring file bytes rather than scene
	// state - worse than simply not undoing it. Cancel is the escape hatch for a
	// session that went wrong; once Commit has written the file, editing it further
	// is editing an asset, the same as painting a texture in an external tool would
	// be.
	namespace MaskPaint {

		// Starts a session painting `layer_index` of `multi_material`'s mask. Loads
		// the layer's existing mask image (if it names one) so painting starts from
		// what is already there; a layer with no mask yet gets a blank canvas of
		// `default_size` square pixels and a generated file name under the assets
		// path. Fails (with `error` set, no state changed) if a session is already
		// open, the multi-material or layer does not exist, or the existing mask
		// image could not be loaded.
		bool Begin(EditorState& state, const std::string& multi_material, int layer_index,
			std::string& error, int default_size = 1024);

		bool Active();
		// The multi-material/layer a session is painting, and its canvas size. Only
		// meaningful while Active().
		const std::string& CurrentMultiMaterial();
		int CurrentLayer();
		int Width();
		int Height();

		// Applies one brush dab centered at UV `(u, v)` (mesh UV space, the same space
		// the shader samples the mask in - see MultiTexture.hlsli). `radius` is in UV
		// units; `strength` in [-1, 1] raises the channel value inside the brush
		// (negative erases), falling off linearly to 0 at the edge. No-op when no
		// session is open.
		void PaintStroke(float u, float v, float radius, float strength);

		// Writes the canvas to its file (creating the directory if needed), points
		// the layer's `mask` at it, rebuilds the multi-material and marks its file
		// dirty, then ends the session. False (session left open) if the file could
		// not be written.
		bool Commit(EditorState& state, std::string& error);

		// Throws the session's edits away and detaches the live preview, restoring
		// whatever the layer's mask was drawing before Begin. Safe to call when no
		// session is open.
		void Cancel(EditorState& state);
		// Alias for Cancel, named for call sites (removing the layer or the
		// multi-material a session belongs to) that need to drop it without framing
		// that as the user backing out of an edit.
		void End(EditorState& state);

		// The panel section: brush controls plus Paint/Commit/Cancel, drawn under a
		// layer's fields in the Multi-Materials tab. Starts a session on `multi_material`
		// index `layer_index` when the paint button is pressed and none is open yet for
		// this layer.
		void DrawSection(EditorState& state, const std::string& multi_material, int layer_index);
	}
}
