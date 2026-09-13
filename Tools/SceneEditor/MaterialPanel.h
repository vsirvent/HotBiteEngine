#pragma once

#include "SceneEditor.h"

#include <Core/Material.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	// Material authoring: the "Materials" panel plus the operations behind it.
	//
	// Materials live in .mat files, which are shared assets referenced by a level -
	// not part of the level file. So material edits are recorded separately from
	// level edits (File/Save Materials, or the panel's Save button, writes only the
	// dirty .mat files), but File/Save Level flushes any unsaved ones too - see
	// SceneSerializer::Save - so a saved level never leaves an edited material or
	// multi-material behind unsaved. Every edit marks the material's own .mat file
	// dirty; only dirty files are rewritten.
	//
	// A material is keyed by *name* throughout, like entities: MaterialData pointers
	// are shared between the entities using them and are not stable across a reload,
	// so history closures and panel state must capture names.
	namespace MaterialOps {

		// Everything about a material that the panel can edit, captured by value so an
		// undo can put it all back in one step. Texture names are the resolved absolute
		// paths MaterialData holds, not the file-relative ones the .mat stores.
		struct MaterialSnapshot {
			HotBite::Engine::Core::MaterialProps props;
			HotBite::Engine::Core::MaterialTextures texture_names;
			HotBite::Engine::Core::MaterialShaderNames shader_names;
			float tessellation_factor = 0.0f;
			float displacement_scale = 0.0f;
			int tessellation_type = 0;
		};

		// Names of every live material, sorted, excluding the retired ones and the
		// internal "__default_*" stand-ins.
		std::vector<std::string> ListMaterials(EditorState& state);

		bool GetSnapshot(EditorState& state, const std::string& material_name, MaterialSnapshot& out);
		// Writes a snapshot back onto a material and refreshes its thumbnail. Used by
		// undo/redo; does not itself record history.
		bool ApplySnapshot(EditorState& state, const std::string& material_name,
			const MaterialSnapshot& snapshot, std::string& error);

		// Records one undoable property edit, marks the material's file dirty and
		// invalidates its thumbnail. Call after the mutation, with the pre-edit
		// snapshot. A no-op edit records nothing.
		void RecordEdit(EditorState& state, const std::string& material_name,
			const MaterialSnapshot& before);

		// Creates a white material named `name` in `mat_file`, selects it, and records
		// history. `mat_file` must be one of World::GetMaterialFiles().
		bool CreateMaterial(EditorState& state, const std::string& name,
			const std::string& mat_file, std::string& error);

		// Duplicates an existing material under a new name, into the source's file.
		bool DuplicateMaterial(EditorState& state, const std::string& source_name,
			const std::string& new_name, std::string& error);

		// Retires a material and repoints every entity using it at the world's default
		// material, as one undoable action. The MaterialData itself is kept alive by
		// World::RemoveMaterial (removing it would dangle an unrelated material), so
		// undo can fully restore both the material and its users.
		bool RemoveMaterial(EditorState& state, const std::string& name, std::string& error);

		// Points an entity's Material component at `material_name`, undoably. The
		// entity's Material component block is recorded in the level's component
		// deltas, so the assignment survives save/reload.
		bool AssignMaterial(EditorState& state, const std::string& entity_name,
			const std::string& material_name, std::string& error);

		// Names of the entities currently using a material.
		std::vector<std::string> FindUsers(EditorState& state, const std::string& material_name);

		// The compiled shaders available for a given pipeline stage: every .cso next to
		// the executable whose name ends in the stage's suffix ("VS", "PS", ...).
		//
		// Offered as a fixed list rather than a free-text field on purpose.
		// ShaderFactory::GetShader caches a shader under its name *before* checking it
		// loaded as the requested stage, so asking for a pixel shader in a vertex slot
		// would poison that name's cache entry for the rest of the session. A picker
		// built from the naming convention cannot express that mistake.
		//
		// `stage_suffix` is one of "VS", "HS", "DS", "GS", "PS". Cached after the first
		// scan; call RefreshShaderList() after compiling new shaders.
		const std::vector<std::string>& ListShaders(const std::string& stage_suffix);
		void RefreshShaderList();

		// Introduces a brand new shader for `stage` ("VS", "HS", "DS", "GS", "PS"):
		// starts from a copy of `source_shader`'s own .hlsl (the currently selected
		// one - genuinely new content still has to come from somewhere, and a working
		// file is a safer starting point than a hand-guessed stub for every stage but
		// VS/PS), or a minimal placeholder for those two if it has none.
		//
		// The copy is written into the open project's own folder (EditorState::
		// project_root) when there is one, so a custom shader lands with the project
		// using it rather than inside the engine's own tracked source tree; it falls
		// back to the source's own folder, then the current directory. Wherever it
		// lands, the source's own directory is added to the search path for just
		// this one compile (ShaderCompiler::CompileToFile's extra_search_dirs), so a
		// copy relocated away from an engine shader's folder still resolves its
		// quoted, parent-relative #includes ("../Common/...").
		//
		// The bytecode is written as "<new_name>.cso" next to the executable, exactly
		// where ListShaders scans and GetShader<T> loads from. On success
		// `out_cso_name` is that file name, ready to assign to a material's shader
		// slot like any picked one (SetShaders above), and `out_hlsl_path` is the
		// source it was written to (handed straight to a "now edit it" step - not
		// re-resolved through ShaderFactory, which has not loaded the new name yet at
		// this point) - creating the files is not itself undoable (like File/Import
		// Model), only the resulting assignment is.
		bool CreateShaderFile(EditorState& state, const std::string& stage,
			const std::string& source_shader, const std::string& new_name,
			std::string& out_cso_name, std::string& out_hlsl_path, std::string& error);

		// Rebinds a material's shaders, undoably, marking its file dirty. Fails without
		// changing anything if any shader will not load as its stage.
		bool SetShaders(EditorState& state, const std::string& material_name,
			const HotBite::Engine::Core::MaterialShaderNames& names, std::string& error);

		// Rewrites every dirty .mat file. Reports how many were written through
		// state.status_message; false with `error` set if any write failed.
		bool SaveMaterials(EditorState& state, std::string& error);
		bool HasUnsavedMaterials(const EditorState& state);
	}

	// The "Materials" panel: a tab bar over the plain-material editor (thumbnail list
	// plus properties, as before) and the Multi-Materials tab (see
	// MultiMaterialPanel.h) for authoring layer stacks.
	namespace MaterialPanel {
		void Draw(EditorState& state);

		// The plain-material tab's content: the thumbnail list on the left, the
		// selected material's properties on the right. Broken out from Draw() only so
		// the tab bar can host it; nothing outside MaterialPanel.cpp calls it.
		void DrawMaterialsTab(EditorState& state);

		// The property editor for a single material - every field, with drag
		// coalescing, undo history and dirty-file marking already wired up.
		//
		// Shared with the Components panel's Material section so that editing a
		// material is the same operation wherever it is done. Anything that edits
		// material values must go through here rather than writing MaterialProps
		// directly, or the edit is neither undoable nor ever saved.
		void DrawMaterialProperties(EditorState& state, const std::string& material_name,
			bool show_preview);
	}
}
