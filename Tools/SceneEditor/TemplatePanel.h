#pragma once

#include "SceneEditor.h"

#include <string>
#include <vector>

namespace HotBiteEditor {

	// Authoring templates: the "Templates" panel plus the operations behind it.
	//
	// A template is the thing the Asset Browser places into the scene. Until now the
	// only way to get one was to import an .fbx; this is the other way - build one
	// out of component blocks (mesh, material, animation, physics, a game's own
	// components) and place instances of it exactly as if it had come from a file.
	//
	// == Where a template lives ==
	// A template is stored one of two ways, and can be moved between them at any time
	// (TemplateOps::SetStorage):
	//
	//  - In its own .tpl file under <assets>/Templates/, holding
	//    {"name": ..., "components": {...}}. This is a *shared asset* referenced by a
	//    level, not part of it - the same relationship .mat files have (see
	//    MaterialPanel.h) - so these edits are written by File/Save Templates rather
	//    than by saving the level. The one place the two meet is that saving a level
	//    flushes dirty templates first: the level's "templates" array names the .tpl
	//    file, and writing that reference while the file does not exist yet would
	//    produce a level that cannot be reloaded.
	//
	//  - Inline in the level's own "templates" array, as the same {"name", "components"}
	//    object. Right for a template only this level uses; it is saved and undone with
	//    the level and leaves no file behind. `EditorState::inline_templates` is the
	//    editor's record of which templates are stored this way.
	//
	// The engine reads both forms identically, so nothing downstream - instances,
	// component "template" keys, placement - can tell them apart.
	//
	// == What the editor holds, and what the World holds ==
	// World::CreateTemplate owns the registration: it builds the template entity the
	// spawner clones and keeps the full component block to apply to every instance
	// (World.h documents the split and why Physics must not sit on the template
	// entity). Everything here is the editor's half - the undo history, the dirty-file
	// bookkeeping and the Asset Browser listing - and every mutation goes through it
	// so those three cannot come apart.
	//
	// Templates are keyed by *name* throughout, like entities and materials.
	namespace TemplateOps {

		// A whole authored template captured by value, which is the unit the undo
		// history stores: `exists` false means "there was no such template", so one
		// snapshot type covers create, edit, remove and a change of storage alike.
		struct TemplateSnapshot {
			bool exists = false;
			bool inline_in_level = false;
			nlohmann::json components = nlohmann::json::object();
		};

		// Components an authored template always carries, because they are exactly
		// what World::SpawnInstance reads off it when cloning. They are editable but
		// never removable: a template that cannot be spawned is not a template.
		bool IsMandatory(const std::string& component);

		// Names of the authored templates, sorted. `ListPlaceable` is every template
		// including the imported ones, which is what the panel's list shows.
		std::vector<std::string> ListAuthored(const EditorState& state);
		std::vector<std::string> ListPlaceable(const EditorState& state);
		bool IsAuthored(const EditorState& state, const std::string& name);

		bool GetSnapshot(EditorState& state, const std::string& name, TemplateSnapshot& out);
		// Restores a snapshot - creating, replacing or removing the template as needed
		// - with the full editor bookkeeping but no history of its own. This is the
		// primitive every undo/redo closure below is built from.
		bool ApplySnapshot(EditorState& state, const std::string& name,
			const TemplateSnapshot& snapshot, std::string& error);

		// Records one undoable change to `name`, from `before` to its current state.
		// Call after the mutation; a no-op change records nothing. Used directly by
		// the panel's drag widgets, which apply per frame and record once at release.
		void RecordEdit(EditorState& state, const std::string& name,
			const TemplateSnapshot& before);

		// A new template: the default unit cube and white material, immediately
		// placeable and ready to be pointed at real assets. Selects it.
		bool CreateTemplate(EditorState& state, const std::string& name, std::string& error);

		// A template built from a scene entity - the "make a prefab out of this" path.
		// Every registered component the entity has is serialized into the template,
		// minus its Transform position (a template is a *kind* of object, so it starts
		// at the origin and the instance decides where it goes).
		bool CreateFromEntity(EditorState& state, const std::string& entity_name,
			const std::string& template_name, std::string& error);

		bool DuplicateTemplate(EditorState& state, const std::string& source,
			const std::string& new_name, std::string& error);

		// Brings a .tpl authored elsewhere into this project: copies it into
		// <assets>/Templates/, registers it and selects it. This is what File/Import
		// Template does; there is no FBX import any more, because an object worth
		// placing is a template now, and the meshes a template points at come from the
		// level's own FBX assets (see AssetBrowser::EnsureTemplatesScanned).
		bool ImportTemplate(EditorState& state, const std::string& tpl_path, std::string& error);
		void ImportTemplateWithDialog(EditorState& state);

		// Whether the template is stored inline in the level rather than in its own
		// .tpl file, and moving it between the two. Moving to inline schedules the .tpl
		// for deletion at the next save (never before, so an undo needs no file back);
		// moving to a file marks it for writing. Both are one undoable step.
		bool IsInline(const EditorState& state, const std::string& name);
		bool SetStorage(EditorState& state, const std::string& name, bool inline_in_level,
			std::string& error);

		// Unregisters the template. Instances already in the scene keep working - they
		// own their components - but nothing new can be placed from it, and the .tpl
		// file is unlinked when the removal is saved (never before, so undo never has
		// to bring a deleted file back).
		bool RemoveTemplate(EditorState& state, const std::string& name, std::string& error);

		// Add-or-update one component block of a template, and remove one. `SetComponent`
		// records history; `ApplyComponent` is the same edit without it, for a widget
		// that is mid-drag and will record once when the drag ends.
		bool SetComponent(EditorState& state, const std::string& name,
			const std::string& component, const nlohmann::json& value, std::string& error);
		bool ApplyComponent(EditorState& state, const std::string& name,
			const std::string& component, const nlohmann::json& value, std::string& error);
		bool RemoveComponent(EditorState& state, const std::string& name,
			const std::string& component, std::string& error);

		// The component names on a template, in registry order (mandatory ones first,
		// as the registry lists them), and one component's block.
		std::vector<std::string> ListComponents(const EditorState& state, const std::string& name);
		nlohmann::json GetComponent(const EditorState& state, const std::string& name,
			const std::string& component);

		// The three asset pickers, as thin wrappers over SetComponent so an asset swap
		// is one undoable step. An empty animation name clears the template's animation.
		bool SetMesh(EditorState& state, const std::string& name,
			const std::string& mesh_name, std::string& error);
		bool SetMaterial(EditorState& state, const std::string& name,
			const std::string& material_name, std::string& error);
		bool SetAnimation(EditorState& state, const std::string& name,
			const std::string& animation, bool loop, float speed, std::string& error);

		// Attaches / detaches a named animation set on the template's mesh, one
		// undoable step. Animation sets are the FBX files the level loaded skeletons
		// from; a mesh can only play an animation belonging to a set attached to it,
		// which is why this exists at all rather than the animation picker being the
		// whole story.
		bool SetAnimationSet(EditorState& state, const std::string& name,
			const std::string& skeleton_name, bool attached, std::string& error);

		// Names of the mesh assets the level has loaded, sorted, excluding the
		// internal "__default_*" stand-in, and of every animation set it loaded.
		std::vector<std::string> ListMeshes(const EditorState& state);
		std::vector<std::string> ListAnimationSets(const EditorState& state);
		// The animation names the template can currently choose from: everything its
		// mesh already offers plus everything its attached sets bring.
		std::vector<std::string> ListTemplateAnimations(const EditorState& state,
			const std::string& name);

		// Spawns an instance of `name`, with the placement bookkeeping and undo history
		// of the Asset Browser's Place buttons (it is the same call).
		bool PlaceTemplate(EditorState& state, const std::string& name, PlacementMode mode,
			std::string& error);

		// Names of the objects currently placed from a template. Worth asking before
		// removing one: those objects stay in the scene for the session but cannot be
		// rebuilt on the next load, since the record that recreates them names a
		// template that will no longer exist.
		std::vector<std::string> FindInstances(const EditorState& state, const std::string& name);

		// Where an authored template is stored: <assets>/Templates/<name>.tpl, and the
		// form the level's "templates" array references it by ("Templates\<name>.tpl").
		// Both are derived from the world's assets path, which is what World::Load
		// resolves the reference against - so the two cannot drift apart, whichever of
		// the two conventions a level's "path" happens to use.
		std::string TemplateFilePath(const EditorState& state, const std::string& name);
		std::string TemplateReference(const std::string& name);

		// Writes every dirty .tpl and unlinks the removed ones. Reports what it did
		// through state.status_message; false with `error` set if any write failed.
		bool SaveTemplates(EditorState& state, std::string& error);
		bool HasUnsavedTemplates(const EditorState& state);

		// Loads the .tpl files under Assets/Templates/ that are not registered yet, so
		// templates authored in another level of the same project are available here
		// too. Called by AssetBrowser::EnsureTemplatesScanned alongside the .fbx scan.
		void ScanTemplatesFolder(EditorState& state);
	}

	// The "Templates" panel: the list of templates on the left, and the selected
	// one's components on the right - the same component-section shape the Components
	// panel uses, but editing a template's stored JSON rather than a live entity.
	namespace TemplatePanel {
		void Draw(EditorState& state);

		// Opens the panel's "make a template out of the selected entity" prompt on the
		// next frame. Edit/Create Template from Selection routes through here rather
		// than creating a template outright, so the name is asked for in one place -
		// and because an ImGui popup can only be opened from the window scope that
		// owns it, never from a menu handler.
		void RequestTemplateFromSelection(EditorState& state);
	}
}
