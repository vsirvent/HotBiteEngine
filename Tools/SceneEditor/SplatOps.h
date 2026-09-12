#pragma once

#include "SceneEditor.h"

#include <string>

namespace HotBiteEditor {

	// Splat-cloud authoring that reaches past the SplatCloud component's own fields:
	// building the inferred low-poly proxy mesh a cloud needs to cast a shadow or carry
	// a Physics component (the compute-based splat renderer never touches the Mesh/
	// Material draw trees, so a cloud is otherwise invisible to both).
	//
	// Entirely outside the undo history, for the same reason MeshOps::GenerateLod and
	// File/Import Model are: this reaches an *asset* (the cloud, not the entity), and
	// the level records the recipe that produced it (World::GenerateSplatProxy /
	// GetGeneratedSplatProxies) rather than anything on the entity - undoing a proxy
	// generation has no per-entity edit to undo in the first place.
	namespace SplatOps {

		// Builds (or rebuilds) the shadow/collision proxy for the SplatCloud entity
		// `entity_name` draws, and re-attaches it to every entity already using that
		// cloud (World::GenerateSplatProxy). `resolution` is signed-distance grid cells
		// along the cloud's longest axis; `ratio` is the share of the raw
		// reconstruction's vertices the final decimation aims for, in (0, 1].
		//
		// Safe to call again for a cloud that already has one: it builds a whole new
		// mesh/shape pair and re-attaches every entity to it rather than touching the
		// previous pair's data in place - see World::GenerateSplatProxy's own comment
		// for why an in-place regenerate used to crash the editor.
		bool GenerateProxy(EditorState& state, const std::string& entity_name,
			int resolution, float ratio, std::string& generated_name, std::string& error);

		// Takes the SplatCloud entity `entity_name` draws' proxy away from every entity
		// currently wearing it (World::RemoveSplatProxy): Mesh, Bounds and Material are
		// removed, so the entity goes back to drawing only as a splat cloud with no
		// shadow and no Physics collider. False, with `error` set, when that cloud has
		// no generated proxy.
		bool RemoveProxy(EditorState& state, const std::string& entity_name, std::string& error);
	}
}
