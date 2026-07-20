#pragma once

// Must come before DXCore.h (Windows.h): reactphysics3d's headers use
// std::numeric_limits<T>::max()/min() and break if the Windows min/max macros
// are already defined when they're first parsed. Every other consumer of
// DXCore.h in this codebase (DemoGame.cpp, Tools/HotBiteTool/Tools.h) follows
// this same include order for the same reason.
#include <Core/PhysicsCommon.h>
#include <Core/DXCore.h>
#include <GUI\GUI.h>
#include <World.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <set>

#include "EditorCamera.h"

namespace HotBite {
	namespace Engine {
		namespace Core {
			class MainEffect;
			class DOFBokeProcess;
			class BaseDOFProcess;
		}
	}
}

namespace HotBiteEditor {

	// An object placed via the editor's Asset Browser "Place" tool. Persisted to the
	// level's "instances" JSON array on save, and reconstructed via World::SpawnInstance
	// (through World::LoadInstances) the next time the level is loaded.
	struct PlacedInstance {
		std::string name;
		std::string template_name;
		std::string material_name;
		HotBite::Engine::float3 position{ 0.0f, 0.0f, 0.0f };
		HotBite::Engine::float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		HotBite::Engine::float3 scale{ 1.0f, 1.0f, 1.0f };
	};

	// A discovered object template (an .fbx file under the project's Assets/Objects/).
	struct TemplateAsset {
		std::string name;      // template key, matches World::GetTemplateEntities' key
		std::string file_path; // absolute path to the .fbx file
		bool loaded = false;        // has this been passed to World::LoadTemplate this session
		bool newly_imported = false; // added via the Asset Browser's Import button this
									  // session, so SceneSerializer needs to add it to the
									  // level's "templates" array on save (pre-existing
									  // templates discovered by scanning disk are already there)
	};

	// A full copy of an entity's Transform channels, the unit the undo history
	// stores (entities are addressed by name because ids get recycled across a
	// place-undo/redo cycle). Also the physics preview's rewind baseline. Named
	// Inspector::TransformSnapshot everywhere it is used; it lives here only
	// because EditorState has to hold some.
	struct TransformSnapshot {
		HotBite::Engine::float3 position{ 0.0f, 0.0f, 0.0f };
		HotBite::Engine::float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		HotBite::Engine::float3 scale{ 1.0f, 1.0f, 1.0f };
	};

	// Which transform tool the viewport gizmo edits. Switched from the Edit menu
	// (scriptable as `menu "Edit/Gizmo: Rotate"` etc.) or the 1/2/3 keys.
	enum class GizmoMode { Translate = 0, Rotate, Scale };

	// Whether the viewport overlays physics collider wireframes, and for which
	// entities (see PhysicsDebug.h). View state, so it records no undo history.
	enum class ColliderView { Off = 0, Selection, All };

	// An entity created via copy/paste as a clone of another scene entity (as
	// opposed to a template instance). Persisted to the level's "clones" JSON array
	// on save and recreated by World::CloneEntity on the next load; `source` always
	// tracks the *current* name of the source entity (EntityOps keeps it updated
	// across renames/cuts so save can resolve what the loader will see).
	struct ClonedEntity {
		std::string name;   // the clone's entity name
		std::string source; // current name of the entity it was cloned from
	};

	// What Edit/Copy (or Cut) captured. Either a placed-instance record (pasting
	// spawns a fresh instance of the same template) or a reference to a scene
	// entity to clone via World::CloneEntity, plus the display state captured at
	// copy time (a cut source is hidden afterwards, so the paste must not inherit
	// the hidden flags from it).
	struct EntityClipboard {
		enum class Kind { None, Instance, SceneEntity };
		Kind kind = Kind::None;

		PlacedInstance instance; // Kind::Instance: the record to respawn from

		// Kind::SceneEntity:
		std::string source_name;   // current entity name to clone from (EntityOps
								   // keeps this updated across renames and cuts)
		std::string display_name;  // name at copy time, base for "<name>_copy" names
		HotBite::Engine::float3 position{ 0.0f, 0.0f, 0.0f };
		HotBite::Engine::float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		HotBite::Engine::float3 scale{ 1.0f, 1.0f, 1.0f };
		bool visible = true;
		bool scene_visible = true;
		bool cast_shadow = true;
	};

	// Per-entity component edits made this session, to be written to the entity's
	// record on save. This is what keeps component changes *per entity*: an entity
	// created from a template (or covered by a wildcard "Cube*" rule) starts with
	// whatever that template/rule gives it, and this delta is the difference for
	// this one entity alone - so stripping Physics from one instance leaves its
	// siblings untouched, across save and reload.
	//
	// `added` holds the component's serialized state rather than just its name, so
	// the values the user then tweaked in the Inspector persist too.
	struct ComponentDelta {
		std::set<std::string> removed;
		std::map<std::string, nlohmann::json> added;
	};

	// Shared, session-long editor state passed to every panel each frame. Owns the
	// bookkeeping needed to save the scene back out (see SceneSerializer.h).
	struct EditorState {
		HotBite::Engine::World* world = nullptr;

		std::string project_root;       // folder containing config.json
		std::string current_level_path; // absolute path to the currently open level.json

		// The current selection. `selected_entity` is the *primary*: the entity the
		// Components panel edits, the one single-entity commands (copy/cut/rename/
		// focus) act on, and the anchor a shift-click range extends from.
		// `selected_entities` is the whole selection in pick order, and always ends
		// with the primary (both are empty/INVALID together). Never assign either
		// directly - go through the Selection helpers, which keep them consistent.
		HotBite::Engine::ECS::Entity selected_entity = HotBite::Engine::ECS::INVALID_ENTITY_ID;
		std::vector<HotBite::Engine::ECS::Entity> selected_entities;
		HotBite::Engine::float3 inspector_euler_degrees{ 0.0f, 0.0f, 0.0f };
		GizmoMode gizmo_mode = GizmoMode::Translate;
		ColliderView collider_view = ColliderView::Off;

		std::vector<PlacedInstance> placed_instances;
		std::set<HotBite::Engine::ECS::Entity> instance_entity_ids; // entities backed by placed_instances
		std::set<std::string> overridden_entities;                 // FBX-authored entities whose transform was edited

		// Copy/cut/paste and rename bookkeeping (all maintained by EntityOps).
		EntityClipboard clipboard;
		std::vector<ClonedEntity> cloned_entities;  // paste-created clones, in creation
													// order (sources precede dependents)
		std::map<std::string, std::string> renamed_entities; // authored (load-time) name -> current name
		std::set<std::string> removed_entities;     // authored entities deleted via cut
		std::map<std::string, std::string> parked_entities;  // parked (cut) runtime name -> authored
															 // name ("" when not persistable, i.e. a
															 // cut clone)

		// Per-entity component add/remove edits, keyed by entity name. See
		// ComponentDelta above; maintained by ComponentOps, written by SceneSerializer.
		std::map<std::string, ComponentDelta> component_deltas;

		// Component blocks read from the level that this binary has no registered
		// component for, kept verbatim so they can be written straight back out:
		// entity name -> component name -> its JSON.
		//
		// The editor is a different executable from the game, so a game's own
		// components (Marbles' Ball/Star/Trigger, DemoGame's CreatureComponent) can
		// never be in its registry. Without this they would be silently dropped the
		// first time a designer opened a game level and saved it - the components
		// would simply cease to exist in the file. Anything in here is displayed
		// read-only (or through the generic grid) and round-trips untouched.
		std::map<std::string, std::map<std::string, nlohmann::json>> opaque_components;

		std::vector<TemplateAsset> templates; // discovered/imported object templates
		std::string selected_template;        // template name chosen in the Asset Browser

		// Materials panel state. `selected_material` is the material whose properties
		// the panel is editing, by name (the editor's stable key for materials, exactly
		// as entity names are for entities).
		//
		// `dirty_material_files` holds the .mat files edited since the last save, so
		// File/Save Materials rewrites only what changed and the panel can mark unsaved
		// work. Material edits are NOT part of the level file - saving the level does
		// not save them, and vice versa - because a .mat is shared by every level that
		// references it.
		std::string selected_material;
		std::set<std::string> dirty_material_files;

		// Entity grouping shown as a tree in the Entities panel: every group name
		// (kept even while empty) plus the group each entity name belongs to
		// (entities absent from the map are ungrouped). Keyed by entity *name*
		// because entity ids are not stable across sessions. Persisted in the level
		// JSON under a top-level "editor" object the engine loader never reads.
		std::set<std::string> entity_groups;
		std::map<std::string, std::string> entity_group_of; // entity name -> group name

		// Edit/Simulate Physics is a *preview*, not an edit: whatever the bodies do
		// while it runs is thrown away when it is switched off, and the scene rewinds
		// to the authored transforms. This holds the pose every simulated entity
		// rewinds to, by name, captured when the preview was switched on (empty while
		// it is off - see PhysicsPreview.h, which owns both maps).
		//
		// Transform edits made *during* a preview retarget the rewind: the entry is
		// overwritten (by Inspector's CommitTransformEdit), so switching the preview
		// off lands on the edited pose, not the one from before the edit. That is what
		// makes it possible to author while watching bodies settle.
		// Keyed by entity name like every other editor map, so EntityOps'
		// RenameEverywhere re-keys it too.
		std::map<std::string, TransformSnapshot> physics_preview_baseline;
		bool physics_preview_active = false;

		// Raised by whichever surface asked to delete the selection (the Del key or
		// the Entities panel's context menu) and consumed once per frame by the main
		// loop, which deletes a lone entity outright and puts a confirmation modal in
		// front of a multi-entity delete. Routed through the state rather than
		// handled in place because the modal has to be driven from window scope.
		bool delete_requested = false;

		std::string status_message;

		// Panel visibility, driven by the View menu. The Project panel doubles as
		// the pre-level project picker, so it is always drawn until a level loads;
		// afterwards it stays hidden unless re-opened from View.
		bool show_outliner = true;
		bool show_inspector = true;
		bool show_asset_browser = true;
		bool show_material_panel = false;
		bool show_project = false;

		// Set by View/Reset Layout: for one frame every panel re-applies its
		// default position/size unconditionally instead of ImGuiCond_FirstUseEver.
		bool apply_default_layout = false;
	};

	// A menu-bar entry, registered by path (e.g. "File/Save Level"). The ImGui menu
	// bar is built from this list and the automation channel executes entries from it
	// by the same path, so scripted runs exercise exactly the code a mouse click would.
	struct MenuCommand {
		std::string path;               // "<Menu>/<Item>"
		std::function<bool()> enabled;  // nullptr = always enabled
		std::function<void()> action;
		std::function<bool()> checked;  // nullptr = no checkmark (used by View toggles)
	};

	class SceneEditorApp : public HotBite::Engine::Core::DXCore, public HotBite::Engine::ECS::EventListener
	{
	public:
		SceneEditorApp(HINSTANCE hInstance);
		virtual ~SceneEditorApp();

		void ForwardWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void ClearScreen(const float color[4]) override;
		void Present() override;

		HotBite::Engine::ECS::Coordinator* GetCoordinator() override;

		void OpenProject(const std::string& project_root);
		bool OpenLevel(const std::string& level_json_path);
		void CloseLevel();

		bool IsLevelLoaded() const { return level_loaded; }
		EditorState& GetState() { return state; }
		EditorCamera& GetEditorCamera() { return editor_camera; }

		// The DOF stage of the post-process pipeline installed on level load (null
		// until then). Focus/amplitude live here; the on/off switch is
		// RenderSystem::SetDOF like every other render feature.
		HotBite::Engine::Core::BaseDOFProcess* GetDofEffect();

		// Runs the menu command registered under `path` exactly as if it were clicked,
		// honoring its enabled() predicate. Returns false with `error` set for an
		// unknown or currently disabled command.
		bool ExecuteMenuCommand(const std::string& path, std::string& error);
		const std::vector<MenuCommand>& GetMenuCommands() const { return menu_commands; }

		// Saves the current backbuffer (including the ImGui UI already rendered into
		// it this frame) as a PNG. Only meaningful between the UI render and the DXGI
		// present, which is when EditorAutomation::OnFrameEnd runs.
		bool CaptureBackBuffer(const std::string& png_path, std::string& error);

	private:
		HotBite::Engine::World world;
		bool level_loaded = false;
		EditorCamera editor_camera;

		EditorState state;
		std::vector<MenuCommand> menu_commands;

		// Post-process chain installed on level load (Marbles-style:
		// MainEffect -> DOFBokeProcess -> backbuffer). Without a pipeline the
		// RenderSystem skips the deferred light mix, ray tracing, AA and motion
		// blur entirely and the scene presents as a flat base pass.
		HotBite::Engine::Core::MainEffect* post_effect = nullptr;
		HotBite::Engine::Core::BaseDOFProcess* dof_effect = nullptr;
		HotBite::Engine::Core::LensEffect* lens_effect = nullptr;
		UI::GUI* gui = nullptr;

		void DrawMenuBar();
		void DrawDeleteRequest();
	};

}
