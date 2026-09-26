#pragma once

#include "SceneEditor.h"

#include <d3d11.h>
#include <string>
#include <utility>
#include <vector>

namespace HotBiteEditor {

	// Poly Haven (polyhaven.com) as a material source: browse its texture catalog by
	// category with previews, and import one as a material of this level.
	//
	// An import is two things, and only the second is scene state:
	//  - the maps are downloaded into <project>/Assets/Textures/PolyHaven/<asset id>/,
	//    one folder per material, exactly as if they had been imported by hand - so
	//    the Textures panel lists them and the material pickers offer them. Like any
	//    texture import this is outside undo.
	//  - a material named after the asset is created in a .mat file (or, if one of
	//    that name already exists, repointed at the new maps - which is how to switch
	//    resolution) and its slots filled. That goes through MaterialOps, so it is
	//    undoable, marks the file dirty and saves with File/Save Materials.
	// Applying it is then the ordinary material assignment (the Components panel, the
	// tab's "Apply to selection", or `assign_material`).
	//
	// Maps: Diffuse -> diffuse, nor_dx -> normal (DirectX convention, which is this
	// engine's), arm -> arm (R=AO, G=roughness, B=metal, the same packing the ARM slot
	// reads), or AO -> ao when an asset has no arm map. Displacement -> height, which
	// switches parallax on; ImportOptions::height opts out of it.
	//
	// All network work runs on background threads; nothing here blocks the frame. An
	// import's download finishes on a worker and is turned into a material by Tick(),
	// on the main thread between frames, because materials are not thread-safe.
	//
	// The source is https://api.polyhaven.com by default. SetSource() points it at a
	// local folder laid out like the API (categories/textures, assets, files/<id>),
	// whose URLs may be plain paths - that is how the regression suite runs offline.
	namespace PolyHaven {

		struct Asset {
			std::string id;          // the key in every URL; also the material's name
			std::string name;        // display name
			std::vector<std::string> categories;
			std::vector<std::string> tags;
			std::string thumbnail_url;
			int download_count = 0;
		};

		enum class CatalogState { Idle, Loading, Ready, Failed };

		// The API root (or local folder) the catalog and downloads come from.
		const std::string& Source();
		// Replaces the source and drops everything fetched from the old one. "" or
		// "default" restores https://api.polyhaven.com.
		void SetSource(const std::string& source);

		// Fetches the category list and the texture catalog in the background (again,
		// when `force`). The panel calls this when first shown.
		void RequestCatalog(bool force = false);
		CatalogState GetCatalogState(std::string* error = nullptr);
		// (name, count) in the API's order, "all" first. Empty until Ready.
		std::vector<std::pair<std::string, int>> Categories();
		// Assets in `category` ("" or "all" = every one) whose id/name/tags contain
		// `filter`, most downloaded first. Empty until Ready.
		std::vector<Asset> Assets(const std::string& category, const std::string& filter);
		bool FindAsset(const std::string& id, Asset& out);

		enum class ThumbState { None, Loading, Ready, Failed };
		// The asset's preview, downloaded in the background on first ask and cached on
		// disk (%TEMP%\HotBitePolyHaven\thumbs) across sessions. Null until it arrives.
		ID3D11ShaderResourceView* Thumbnail(const std::string& id, ThumbState* state = nullptr);
		// Releases the preview textures (called when a level opens, like the Textures
		// panel's thumbnails). The downloaded files stay cached.
		void ReleaseThumbnails();

		struct ImportOptions {
			std::string resolution = "1k";  // "1k" "2k" "4k" "8k"; falls back to the nearest smaller one
			std::string mat_file;           // "" = the selected material's file, else the level's first
			bool height = true;             // import Displacement into the height slot (turns parallax on)
		};

		// Starts downloading `id`'s maps. Fails at once (nothing started) when another
		// import is running, no project or level is open, the level has no .mat file,
		// or the id is unknown. The material appears a few frames after the download
		// finishes - poll ImportBusy()/LastImportResult().
		bool StartImport(EditorState& state, const std::string& id, const ImportOptions& options,
			std::string& error);
		bool ImportBusy();
		// One line describing the running import ("rock_wall_08: 2/3 maps").
		std::string ImportProgress();
		// The outcome of the last finished import: true with the material's name in
		// `message`, or false with the reason.
		bool LastImportResult(std::string& message);

		// Where an asset's maps go, relative to Assets/Textures ("PolyHaven\<id>").
		std::string TextureFolder(const std::string& id);

		// Main thread, once per frame between frames: turns a finished download into
		// a material. Inert when nothing finished.
		void Tick(EditorState& state);

		// Stops the workers. Called before the device and ImGui go away.
		void Shutdown();

		// The Materials panel's "Poly Haven" tab.
		void DrawTab(EditorState& state);

		// Opens the Materials panel on this tab (the `polyhaven_show` command). The tab
		// bar reads it once through ConsumeShowRequest, the next time it draws.
		void Show(EditorState& state);
		bool ConsumeShowRequest();
	}
}
