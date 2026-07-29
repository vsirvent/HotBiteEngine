#pragma once

#include "SceneEditor.h"

#include <Core/Material.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// Multi-material authoring: the Materials panel's "Multi-Materials" tab and the
	// operations behind it.
	//
	// A multi-material is a named stack of layers - "dirt, grass and rock through one
	// mask image, with snow on whatever faces up". It is an asset exactly like a
	// material: it lives in a .mat file's "multi_materials" array, is shared by every
	// level referencing that file, and is written by File/Save Materials rather than
	// by saving the level. See the Multi-materials block in World.h for the engine
	// half, and Core/Material.h for what a layer can say.
	//
	// A *material* wears one (Assign below), which is what makes surfaces use it -
	// never an entity. The render trees are keyed by material, so one bucket draws
	// with one set of layer constants no matter how many entities it holds.
	//
	// Everything here is keyed by name, like materials and entities, because pointers
	// into the world's registries are not stable across a reload and an undo closure
	// has to survive one.
	namespace MultiMaterialOps {

		// The whole authoring record, captured by value so one undo puts it all back.
		// This is the engine's own type: it carries the layers and the surface
		// parameters, and its derived GPU arrays are rebuilt from those on apply.
		using Snapshot = HotBite::Engine::Core::MultiMaterialData;

		// Names of every live multi-material, sorted, excluding retired ones.
		std::vector<std::string> List(EditorState& state);

		bool GetSnapshot(EditorState& state, const std::string& name, Snapshot& out);
		// Writes a snapshot back and rebuilds it. Used by undo/redo; records nothing.
		bool ApplySnapshot(EditorState& state, const std::string& name,
			const Snapshot& snapshot, std::string& error);
		// Records one undoable edit and marks the owning .mat file dirty. Call after
		// the mutation, with the pre-edit snapshot; a no-op edit records nothing.
		void RecordEdit(EditorState& state, const std::string& name, const Snapshot& before);

		// Creates an empty stack named `name` in `mat_file` and selects it.
		bool Create(EditorState& state, const std::string& name,
			const std::string& mat_file, std::string& error);
		bool Duplicate(EditorState& state, const std::string& source_name,
			const std::string& new_name, std::string& error);
		// Retires a stack and detaches it from every material wearing it, as one
		// undoable action.
		bool Remove(EditorState& state, const std::string& name, std::string& error);

		// Layer editing. `material` is the name of the material supplying the new
		// layer's maps; layers blend in order, index 0 first.
		bool AddLayer(EditorState& state, const std::string& name,
			const std::string& material, std::string& error);
		bool RemoveLayer(EditorState& state, const std::string& name, int index,
			std::string& error);
		// Moves a layer by `delta` places, clamped to the ends (a no-op at an end
		// rather than an error, so a UI arrow can stay enabled).
		bool MoveLayer(EditorState& state, const std::string& name, int index, int delta,
			std::string& error);

		// Sets any subset of a layer's fields from a JSON object, by the same key
		// names the .mat file uses plus the flattened forms the automation channel
		// takes ("slope_min", "height_fade", ...). One undoable step. This is the one
		// mutation entry point for layer values - the panel widgets use it too, so a
		// scripted edit and a dragged slider are literally the same operation.
		bool SetLayer(EditorState& state, const std::string& name, int index,
			const nlohmann::json& fields, std::string& error);
		// The same for the stack's own parameters: parallax_scale, tess_type,
		// tess_factor, displacement_scale.
		bool SetParams(EditorState& state, const std::string& name,
			const nlohmann::json& fields, std::string& error);

		// Attaches a stack to a material, or detaches it when `name` is empty. The
		// material's .mat file is marked dirty; undoable.
		bool Assign(EditorState& state, const std::string& material_name,
			const std::string& name, std::string& error);
		// Names of the materials currently wearing a stack.
		std::vector<std::string> FindMaterials(EditorState& state, const std::string& name);
		// Names of the entities drawing with any material that wears it.
		std::vector<std::string> FindUsers(EditorState& state, const std::string& name);

		// A layer as JSON, for the automation channel's readback. Empty object for a
		// bad name or index.
		nlohmann::json LayerJson(EditorState& state, const std::string& name, int index);
	}

	namespace MultiMaterialPanel {
		// The Multi-Materials tab: the list on the left, the selected stack's layers
		// on the right. Drawn inside the Materials panel's tab bar.
		void Draw(EditorState& state);
	}
}
