#pragma once

#include "SceneEditor.h"

namespace HotBiteEditor {
	// Entity-level clipboard and naming operations: rename, copy, cut, paste.
	// Every successful mutation records one EditorHistory action, so all surfaces
	// (Components panel, Entities panel context menu, Edit menu, Ctrl+C/X/V, and
	// the automation `rename`/`copy`/`cut`/`paste` commands) go through here.
	//
	// What can be copied/cut:
	//  - editor-placed template instances (any part of one selects the whole
	//    instance); pasting spawns a fresh instance of the same template.
	//  - mesh entities (Base+Transform+Bounds+Mesh): FBX-authored ones and
	//    previous pastes; pasting clones them via World::CloneEntity.
	//  Lights, cameras and the sky are refused - their components own live GPU
	//  or system resources a component copy would alias.
	//
	// Cut does not destroy an FBX-authored/cloned entity immediately (its mesh
	// data must stay clonable for a later paste, and undo must be able to bring it
	// back with its physics body intact). Instead the entity is *parked*: renamed
	// to a "__cut_..." name, hidden, and its rigid body deactivated. Parked
	// entities are filtered out of the Entities panel and `list_entities`, and the
	// save code skips them everywhere; on the next level load they are genuinely
	// gone (saved under "removed_entities"). Cut instances just despawn (they can
	// be respawned from their template at any time).
	namespace EntityOps {

		// Entities whose name carries this prefix are parked cut entities: hidden,
		// inert, skipped by UI listings and by the serializer.
		bool IsParkedName(const std::string& name);

		// Renames an entity, updating every piece of editor bookkeeping that is
		// keyed by entity name (instance records, clone records and their sources,
		// transform-override tracking, group membership, the clipboard, and the
		// authored-name map the serializer persists renames through). Multi-part
		// instance parts cannot be renamed (their "<instance>_<index>" names must
		// stay derivable from the instance record).
		bool RenameEntity(EditorState& state, const std::string& old_name,
			const std::string& new_name, std::string& error);

		// Captures the selected entity into the clipboard. Copy leaves the scene
		// untouched (and records no history); cut also removes the entity from the
		// scene (one undoable action).
		bool CopySelected(EditorState& state, std::string& error);
		bool CutSelected(EditorState& state, std::string& error);

		// Creates a new entity from the clipboard at the transform captured at
		// copy time, named "<original>_copy", "<original>_copy2", ... The clipboard
		// survives, so repeated pastes create numbered copies.
		bool Paste(EditorState& state, std::string& error);

		// Whether the current selection is something CopySelected/CutSelected
		// accepts (drives the Edit menu enabled state).
		bool CanCopySelected(EditorState& state);

		// Removes every deletable entity in the selection (mesh entities are parked
		// like a cut, placed instances despawn) as ONE undoable action, leaving the
		// clipboard alone. Entities the editor refuses to remove - lights, cameras,
		// the sky - are skipped rather than failing the whole delete. Returns false
		// with `error` set when nothing in the selection could be deleted.
		// The Del key and the Entities panel both route here; confirming a
		// multi-entity delete is the caller's job (SceneEditorApp::Present).
		bool DeleteSelected(EditorState& state, std::string& error);
		bool CanDeleteSelected(EditorState& state);
	}
}
