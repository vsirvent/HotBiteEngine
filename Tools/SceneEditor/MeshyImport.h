#pragma once

#include "SceneEditor.h"

#include <functional>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// Importing a Meshy AI (meshy.ai) model export as a placeable, textured, lit
	// object - the "I downloaded a model from Meshy and want it in my scene" path.
	//
	// A Meshy export is one .fbx plus a handful of loose PBR maps sitting next to it:
	// <stem>.png (diffuse), and any of <stem>_normal / _metallic / _roughness / _ao /
	// _emissive / _height / _opacity .png|.jpg. Loose, because FBX's classic Phong/
	// Lambert material has no metallic/roughness channel to carry them in - Meshy
	// (like every such exporter) hands them over separately for the target
	// application to wire up by hand, which is exactly the gap this closes.
	//
	// This is *not* a replacement for File/Import Model: AssetBrowser::ImportModel
	// still does the first half (copy the .fbx into Assets/Objects, register it, load
	// its mesh - see SceneEditor.h's model/template/instance split). What this adds is
	// the second half a plain FBX import cannot: the FBX's own material has no working
	// maps (its texture links point at files this package does not carry, or nowhere
	// at all), so CreateFromModel alone would leave the object textureless. Import()
	// brings the loose maps in as a proper material and hands back a template wearing
	// it - Mesh, Material and Bounds from CreateFromModel, Lighted added automatically
	// by the spawner (World.cpp), the same set any placed object carries.
	//
	// AO goes straight into the material's plain ao slot. Metallic and roughness have
	// no slot of their own - the engine's material model has no metallic/roughness
	// workflow (Core::Material.h) - so whichever of the two is present feeds the plain
	// spec slot instead (metallic first, roughness only as a last resort): a metallic
	// map is the closer stand-in for "how shiny is this surface", which is all the
	// engine's Blinn-Phong-ish spec model is asking for. An explicit specular map, on
	// the rare package that has one, always takes priority over both.
	//
	// RIGGED PACKAGES. A Meshy export with a skeleton is several .fbx files instead
	// of one: "<base>_Character_output.fbx" (the skinned model), and one
	// "<base>_Animation_<Clip>_without_skin.fbx" per clip (skeleton and motion, no
	// mesh), with the maps named "<base>_texture_0[_<map>].png". It is recognised by
	// the "_Character_output" name, and Import then also:
	//   - imports every animation file as a model of its own, "<name>_<clip>" (the
	//     engine's unit for an animation set), under the same Assets/Objects/<name>/;
	//   - gives the template a clip library (Mesh "clips"): the clip's label,
	//     lower-cased, is its name - Walking -> walk, Running -> run, anything
	//     "breathe" -> idle - and idle, when there is one, is the default;
	//   - leaves the template upright: the character is authored Z-up. Meshy's FBX
	//     normally carries the -90 degree X turn on the mesh node, which the template
	//     keeps; when the rotated bounds still stand on Z, that turn is added.
	// The clip files are exported from another rig (a "mixamorig:" skeleton against
	// the character's own, with extra leaf joints), which the engine's index-based
	// clip playback cannot use as it is: MeshData::AddSkeleton retargets such a set
	// onto the mesh's skeleton by joint name (Skeleton::RetargetFrom), at load time,
	// so nothing extra is saved and a reopened level resolves the clips the same way.
	// A folder is refused as "more than one .fbx" only when it holds several
	// characters; the animation files do not count.
	namespace MeshyImport {

		// Runs the whole pipeline against `source_path`, which may be:
		//   - a .zip, extracted with the tar.exe Windows already ships (no bundled
		//     zip library) into a throwaway temp folder;
		//   - a folder already extracted;
		//   - the .fbx file itself, picked directly (its own folder is then searched
		//     for the loose maps, and picking the file this way is the way to
		//     disambiguate a folder that happens to hold more than one .fbx).
		//
		// `name` becomes the registry key for all three things this creates - the
		// model, the material and the template - exactly one of each, so a collision
		// on any of the three is checked up front and refused before anything is
		// imported (a Meshy import that got the model in but failed on the material
		// would otherwise leave a half-finished result behind). Empty `name` falls
		// back to the source file's stem, like AssetBrowser::ImportModel.
		//
		// Everything this pulls in lands under `name`'s own subfolder rather than
		// mixed flat into the project's asset folders: the .fbx at
		// Assets/Objects/<name>/ (AssetBrowser::ImportModel's own `subfolder`) and
		// every texture at Assets/Textures/<name>/ - so everything one Meshy package
		// brought in is one folder to find, move or delete.
		//
		// `lod_ratios`, when not empty, is a finest-first list of extra levels of
		// detail to generate from the imported mesh in the same step (each a share
		// of the *full* mesh's vertex count, in (0,1) and decreasing - four total
		// levels is `{0.5f, 0.25f, 0.125f}`, level 0 being the mesh as imported).
		// Built through World::GenerateMeshLod exactly as the Components panel's own
		// "Generate level" button is (see MeshOps.h) - always from the full mesh,
		// never chained level-over-level, for the reason documented there - and
		// installed straight onto the template's Mesh block, so a placed instance
		// already has the chain rather than needing it added by hand afterwards.
		// The levels are named `<name>_lod<n>` (not after the .fbx's node, which
		// unrelated exports often share) and cached beside the model, in
		// Assets/Objects/<name>/, so they cannot collide with another object's.
		//
		// `on_progress` mirrors World::LoadModel's - this can take a while for a
		// large export, and is meant to be driven through the same loading-overlay
		// path as a plain model import (see SceneEditorApp::ImportMeshyModelWithProgress).
		bool Import(EditorState& state, const std::string& source_path,
			const std::string& name, std::string& out_template_name, std::string& error,
			std::function<void(float, const std::string&)> on_progress = nullptr,
			const std::vector<float>& lod_ratios = {});

		// The default halving sequence for `count` extra LOD levels beyond the full
		// mesh (0.5, 0.25, 0.125, ...) - what the naming popup pre-fills and what
		// `import_meshy_model` falls back to when it is given a level count but no
		// ratios of its own. Mirrors MeshOps::SuggestedRatio's halving convention so a
		// chain built this way looks like one built by hand, one level at a time.
		std::vector<float> DefaultLodRatios(int count);

		// File/Import Meshy Model...: a file dialog (.zip, .fbx, or "All files" to
		// pick a folder's worth by hand is not offered - Windows' common dialog picks
		// files, not folders), then the same name-before-load popup File/Import Model
		// uses (drawn by AssetBrowser, which owns EditorState::pending_meshy_import_path/
		// name) - the name is asked for before anything loads because it becomes the
		// key everything this import creates is registered under.
		void ImportWithDialog(EditorState& state);
	}
}
