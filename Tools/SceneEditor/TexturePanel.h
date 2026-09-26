#pragma once

#include "SceneEditor.h"

#include <d3d11.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// The project's texture library: everything under <project>/Assets/Textures,
	// managed on its own, not through a material. Load a whole set, keep it there, and
	// decide later which material slot uses what - a material's texture pickers
	// (MaterialPanel) offer exactly this folder.
	//
	// The library *is* the folder - there is no separate list to fall out of step with
	// it - so adding is copying files in and removing is deleting them. Both are
	// outside undo, like File/Import Model: a file is not scene state. Removal is
	// therefore guarded instead: a texture a material (or a multi-material layer's
	// mask) still names cannot be removed until nothing uses it.
	//
	// Textures are addressed by their name relative to Assets/Textures, with the
	// platform separator ("Wood\Planks\a.png"), which is what MaterialOps::ListTextures
	// reports.
	namespace TextureOps {

		// Every subfolder of Assets/Textures (relative, sorted), empty ones included -
		// ListTextures only sees files, and a folder made ahead of an import has none.
		std::vector<std::string> ListFolders(const EditorState& state);

		// What still names `texture`: "<material> (<slot>)" for a material's map,
		// "<multi-material> layer <n> mask" for a layer's mask image.
		std::vector<std::string> FindUsers(EditorState& state, const std::string& texture);

		// Copies each file in (non-images are skipped and counted in `skipped`) into
		// Assets/Textures/<subfolder>.
		bool ImportFiles(EditorState& state, const std::vector<std::string>& files,
			const std::string& subfolder, int& imported, int& skipped, std::string& error);

		// Copies every image under `folder`, recursively, into Assets/Textures/<subfolder>,
		// keeping the folder's own structure - "load a full set".
		bool ImportFolder(EditorState& state, const std::string& folder,
			const std::string& subfolder, int& imported, int& skipped, std::string& error);

		bool CreateFolder(EditorState& state, const std::string& folder, std::string& error);

		// Deletes a texture file. Refused (nothing touched) while it is in use.
		bool RemoveTexture(EditorState& state, const std::string& texture, std::string& error);

		// Deletes a folder and everything in it. Refused (nothing touched) if any texture
		// inside is in use; `removed` is the number of textures deleted.
		bool RemoveFolder(EditorState& state, const std::string& folder, int& removed,
			std::string& error);

		// Drops the panel's cached thumbnails (called when a level opens).
		void ReleaseThumbnails();

		// The texture as a shader resource, loaded on first ask and cached (null if the
		// file will not load). Shares the engine's texture cache, so it is the same SRV a
		// material using the file holds.
		ID3D11ShaderResourceView* Thumbnail(const EditorState& state, const std::string& texture);
	}

	namespace TexturePanel {
		void Draw(EditorState& state);
	}
}
