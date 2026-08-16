#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace AssetBrowser {
		void Draw(EditorState& state);

		// Brings the project's asset layers into `state`: the .fbx files under
		// Assets/Objects (and any the level itself loaded) as *models*, and the .tpl
		// files under Assets/Templates as templates. Loading a model registers its
		// meshes, materials and animation clips with the World and places nothing -
		// making a template is the separate, explicit step.
		// Draw calls this lazily on project change; automation calls it before model
		// and template commands so both see the same lists.
		void EnsureAssetsScanned(EditorState& state);

		// Imports one .fbx as a model: copies it into Assets/Objects when it comes
		// from outside the project, loads it and selects it. This is File/Import
		// Model...; it deliberately creates no template (TemplateOps::CreateFromModel
		// is the next step, one click away in the panel).
		bool ImportModel(EditorState& state, const std::string& fbx_path, std::string& error);
		void ImportModelWithDialog(EditorState& state);

		// The world point the middle of the viewport is aimed at: the first surface
		// the view-center ray hits (the same raycast a viewport click runs), or a
		// fixed distance down that ray when it hits nothing. False when there is no
		// camera to aim with, in which case `out` is untouched.
		//
		// Where every ViewCenter placement starts from, before the placed object's
		// own footprint and base transform are taken out of it.
		bool ViewCenterPoint(EditorState& state, HotBite::Engine::float3& out,
			bool& hit_something);

		// Spawns an instance of `template_name`, records it for save and selects it.
		// `mode` decides where it lands (see PlacementMode). Returns false with `error`
		// set on failure; `out_position`, when given, receives the world position the
		// instance ended up at, which the callers report back to the user.
		bool PlaceTemplate(EditorState& state, const std::string& template_name,
			PlacementMode mode, std::string& error,
			HotBite::Engine::float3* out_position = nullptr);

		// Spawns `inst` into the world with the full save/selection bookkeeping, and
		// removes an instance (every part of a multi-part template) with the reverse
		// bookkeeping. The primitives place/paste/cut and their undo closures are
		// built from; they record no history themselves.
		bool SpawnRecordedInstance(EditorState& state, const PlacedInstance& inst, std::string& error);
		void RemovePlacedInstance(EditorState& state, const std::string& instance_name);

		// The entity names World::SpawnInstance gives one instance of `template_name`:
		// "<instance_name>" for a single-part template, "<instance_name>_<index>" for
		// every part of a multi-part one. Anything that has to find an instance's
		// entities after the fact - removal, and re-deriving the bookkeeping on load -
		// has to reproduce that naming, so it lives here rather than in each caller.
		std::vector<std::string> InstancePartNames(EditorState& state,
			const std::string& instance_name, const std::string& template_name);
	}
}
