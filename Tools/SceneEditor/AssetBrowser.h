#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	namespace AssetBrowser {
		void Draw(EditorState& state);

		// Scans the project's Assets/Objects folder for .fbx files and loads any
		// not-yet-loaded ones into the World. These are not imported *as* objects any
		// more - importing means importing a template (see TemplateOps::ImportTemplate)
		// - but the scan stays, because it is how a project's FBX meshes, materials and
		// animation sets become available for a template to point at, and because a
		// level may still list an .fbx template that instances refer to.
		// Draw calls this lazily on project change; automation calls it before template
		// commands so both see the same list.
		void EnsureTemplatesScanned(EditorState& state);

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
