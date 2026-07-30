#pragma once

#include "SceneEditor.h"

#include <Core/Mesh.h>
#include <string>

namespace HotBiteEditor {

	// Mesh-asset authoring that is more than setting a field: right now, building a
	// level of detail instead of picking one.
	//
	// A LOD is normally a second model somebody exported next to the first, which
	// means a chain cannot be authored at all without going back to the modelling
	// tool. GenerateLod produces one from the mesh the entity is already drawing
	// (Core::SimplifyMesh, through World::GenerateMeshLod) and registers it as a mesh
	// asset of its own - so from the moment it exists it is indistinguishable from an
	// imported mesh: it shows up in the mesh pickers, it can be given to another
	// entity, and the level records it so the next load has it too.
	//
	// Two things are deliberately outside the undo history, for the same reason
	// File/Import Model and mask painting are (see MaskPaint.h): the mesh *asset*
	// and the file it was cached into. Undoing the edit below takes the level back
	// off the chain - which is the change to the scene - and leaves the asset where
	// it is, exactly as undoing a placement leaves the imported model loaded.
	namespace MeshOps {

		// Generates a coarser stand-in for the mesh `entity_name` draws and appends it
		// to that mesh's chain, as one undoable Mesh-component edit. `ratio` is the
		// share of the full mesh's vertices to aim for, in (0, 1).
		//
		// Like everything else about a chain, this reaches the mesh *asset*: every
		// entity drawing that mesh gains the level. The entity is only where the edit
		// is recorded.
		bool GenerateLod(EditorState& state, const std::string& entity_name, float ratio,
			std::string& generated_name, std::string& error);

		// What to offer as the next level's ratio: half of the coarsest level in the
		// chain, so repeatedly accepting the default builds the usual halving sequence
		// (50%, 25%, 12.5%) instead of three levels of the same size - which is what a
		// fixed default produces, and what makes a chain that never switches twice.
		float SuggestedRatio(const HotBite::Engine::Core::MeshData* data);
	}
}
