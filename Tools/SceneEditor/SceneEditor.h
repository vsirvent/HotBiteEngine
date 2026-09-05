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

	// The project's three asset layers, and the one rule that keeps them apart:
	//
	//   Model     an imported .fbx. It contributes meshes, materials and animation
	//             clips, and nothing else - it is not an object and cannot be placed.
	//   Template  a named entity definition: components with values, one concept
	//             ("troll"), built out of the assets models brought in. The only
	//             placeable kind of thing.
	//   Instance  a template placed in this level (see PlacedInstance).
	//
	// Everything below follows from that. See World.h for the engine's half.

	// An imported asset file. `name` is the file stem, which is the key World's model
	// registry uses; what the file actually brought in is asked of the World
	// (GetModelAssets) rather than cached here, so the panel and the engine cannot
	// disagree about it.
	struct ModelAsset {
		std::string name;
		std::string file_path; // absolute path of the .fbx
		bool loaded = false;   // has this been passed to World::LoadModel this session
	};

	// A template the editor can place: a component block authored in the Templates
	// panel, stored either in its own .tpl under Assets/Templates/ or inline in the
	// level (see TemplatePanel.h).
	struct TemplateAsset {
		std::string name;      // template key, matches World::GetTemplateEntities' key
		std::string file_path; // the .tpl it lives in (or would live in, while it is
							   // stored inline in the level instead - see
							   // inline_templates)
		bool loaded = false;   // registered with the World this session
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

	// Where a newly placed template instance lands.
	enum class PlacementMode {
		// The world origin. Predictable and scriptable, and the only sensible answer
		// when there is no camera yet.
		Origin,
		// Resting on the first thing the middle of the view is looking at: the
		// object's own bounding box is used to sit it *on* that surface rather than
		// through it. Falls back to a fixed distance in front of the camera when the
		// view center hits nothing (sky, or an empty scene).
		ViewCenter,
	};

	// Which transform tool the viewport gizmo edits. Switched from the Edit menu
	// (scriptable as `menu "Edit/Gizmo: Rotate"` etc.) or the 1/2/3 keys.
	enum class GizmoMode { Translate = 0, Rotate, Scale };

	// Whether the viewport overlays physics collider wireframes, and for which
	// entities (see PhysicsDebug.h). View state, so it records no undo history.
	enum class ColliderView { Off = 0, Selection, All };

	// Which shadow debug tint the engine renders the scene with (see ShadowDebug.h).
	// One at a time: both recolour the same directional term, so showing them together
	// would multiply two palettes into a colour that means nothing.
	//   Cascades  - which cascade slice shaded each pixel (dynamic casters).
	//   StaticMap - what the single static-caster map covers, and what it shadows.
	enum class ShadowDebugView { Off = 0, Cascades, StaticMap };

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
	// the values the user then tweaked in the Inspector persist too - and, by the
	// same mechanism, it holds components the entity always had whose *fields* were
	// edited (ComponentOps::ApplyValue). Both end up as one block in the entity's
	// record, which is what the loader applies on top of whatever the FBX or the
	// template gave it.
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

		// Grid snapping for the gizmo (SelectionGizmo.cpp) and template placement
		// (AssetBrowser.cpp's ViewCenterPosition). Persisted per level under
		// "editor"/"grid" alongside entity_groups below (see SceneSerializer.cpp) -
		// it is editor-only state the engine loader never reads.
		bool grid_snap_enabled = false;
		float grid_size = 1.0f;                    // world units a translate snaps to
		float grid_rotation_step_degrees = 15.0f;  // degrees a rotate drag snaps to
		float grid_scale_step = 0.1f;              // scale-factor step a scale drag snaps to
		ColliderView collider_view = ColliderView::Off;
		// Which shadow debug tint the scene is rendered with (see ShadowDebug.h). View
		// state, so it records no undo history - but unlike the collider overlay it
		// switches a flag on the light itself, so ShadowDebug::Draw has to push it every
		// frame, Off included, or a light would keep tinting after the view was changed.
		ShadowDebugView shadow_debug_view = ShadowDebugView::Off;

		std::vector<PlacedInstance> placed_instances;
		std::set<HotBite::Engine::ECS::Entity> instance_entity_ids; // entities backed by placed_instances
		std::set<std::string> overridden_entities;                 // FBX-authored entities whose transform was edited

		// Entities created empty in this level (Add/Entity), in creation order. An
		// entity built this way comes from no model, no template and no other entity,
		// so unlike every other kind the level has to record that it exists at all:
		// SceneSerializer writes the list (with each entity's live transform and its
		// components) to the level's "created_entities" array, and World::Load
		// rebuilds them from it. Names, like every other editor map, so a rename or an
		// undo that re-creates the entity still resolves.
		std::vector<std::string> created_entities;

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

		std::vector<TemplateAsset> templates; // the project's placeable templates
		std::string selected_template;        // template name chosen in the Asset Browser
											  // or the Templates panel (they share it, so
											  // selecting in one shows it in the other)

		// The imported asset files this project has loaded, and the one selected in
		// the Asset Browser's Models section. A model is never placed - it is picked
		// to inspect what it brought in, or to build a template out of.
		std::vector<ModelAsset> models;
		std::string selected_model;

		// File/Import Model... between picking the file and naming it. The name is
		// asked for before the load rather than after, because it is the key the
		// model's assets are registered under and renaming afterwards would mean
		// re-registering them; empty when no import is pending.
		std::string pending_import_path;
		std::string pending_import_name;

		// Templates panel state (see TemplatePanel.h). An authored template lives in
		// its own .tpl file under Assets/Templates/, a shared asset the level merely
		// references - so, like materials, template edits are written by File/Save
		// Templates rather than by saving the level.
		//
		// `dirty_templates` holds the ones edited since the last save; `removed_templates`
		// holds the ones deleted this session whose .tpl file is still on disk, so the
		// file is only unlinked when the removal is actually saved (which keeps undo of
		// a removal from having to restore a deleted file).
		//
		// `inline_templates` holds the ones stored *in this level* instead - written
		// into the level's own "templates" array as a {"name", "components"} object, so
		// they are saved (and undone) with the level and never appear in the two sets
		// above. A template can be moved between the two storage forms at any time.
		std::set<std::string> dirty_templates;
		std::set<std::string> removed_templates;
		std::set<std::string> inline_templates;

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

		// The multi-material (layer stack) the Materials panel's second tab is
		// editing, by name. Multi-materials live in the same .mat files and are
		// marked unsaved through the same set above - a stack and the materials it
		// blends are written by one File/Save Materials.
		std::string selected_multi_material;
		// Which layer of it the Layers table has open, and which the mask painting
		// tool targets when a session is started without an explicit index.
		int selected_multi_material_layer = 0;

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

		// Panel visibility, driven by the View menu. Nothing is drawn until a level
		// is open (see SceneEditorApp::Present).
		bool show_outliner = true;
		bool show_inspector = true;
		bool show_asset_browser = true;
		bool show_material_panel = false;
		bool show_template_panel = false;
		bool show_log_panel = false;

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

		// Loads a level, drawing the loading overlay from World::Load's own progress
		// callback (see SceneEditor.cpp). Blocks the calling thread for the whole load
		// and renders frames of its own, so it must NOT be called from inside an ImGui
		// frame - anything running from a menu action or a panel goes through
		// RequestOpenLevel instead.
		bool OpenLevel(const std::string& level_json_path);

		// Queues a level to be opened at the top of the next render tick, before the
		// frame's ImGui pass begins. This is what the File menu (and anything else
		// drawing UI) must use: OpenLevel renders its own progress frames and a nested
		// frame would trip ImGui's Begin/End balance.
		void RequestOpenLevel(const std::string& level_json_path);

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

		// Loading overlay state. A level load blocks the thread that drives rendering,
		// so the progress bar can only advance if the load itself paints frames: these
		// are filled in from World::Load's OnLoadProgress callback (the same hook the
		// demo game passes to World::Load) and drawn by RenderLoadingFrame.
		float loading_progress = 0.0f; // 0..1
		std::string loading_stage;     // what the load is doing right now
		std::string loading_level;     // level file being loaded, shown as the caption

		// Set by RequestOpenLevel, consumed by the render tick (see the constructor).
		std::string pending_level_path;

		void DrawMenuBar();
		void DrawDeleteRequest();
		void DrawGridSettingsPopup();

		// Updates the overlay and paints one frame of it. Called for every phase
		// World::Load reports plus the editor-side steps that follow it.
		void ShowLoadingProgress(float fraction, const std::string& stage);
		void RenderLoadingFrame();
	};

}
