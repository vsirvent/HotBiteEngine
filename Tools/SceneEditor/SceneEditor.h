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

		std::vector<TemplateAsset> templates; // discovered/imported object templates
		std::string selected_template;        // template name chosen in the Asset Browser

		// Entity grouping shown as a tree in the Entities panel: every group name
		// (kept even while empty) plus the group each entity name belongs to
		// (entities absent from the map are ungrouped). Keyed by entity *name*
		// because entity ids are not stable across sessions. Persisted in the level
		// JSON under a top-level "editor" object the engine loader never reads.
		std::set<std::string> entity_groups;
		std::map<std::string, std::string> entity_group_of; // entity name -> group name

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

		// Marbles-style DOF autofocus: whenever the camera moves, the focus distance
		// is recomputed as the depth of whatever sits at the center of the view
		// (scene raycast, falling back to the orbit target against the sky), and
		// the amplitude follows Marbles' distance-based aperture - wide open
		// (macro-like shallow depth of field) up close, fully stopped down (whole
		// scene in focus) from ~20 units out. Marbles does the same continuous
		// refocusing with the player as its subject; the editor's subject is what
		// the camera is aimed at. While enabled both manual DOF sliders are inert.
		// Toggled from the Render menu or the automation channel's
		// `render dof_autofocus 0|1`.
		bool GetDofAutofocus() const { return dof_autofocus; }
		void SetDofAutofocus(bool enabled)
		{
			//Re-arm the camera-motion check so enabling refocuses immediately even
			//from a standstill.
			dof_refocus_pending = dof_refocus_pending || (enabled && !dof_autofocus);
			dof_autofocus = enabled;
		}

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
		UI::GUI* gui = nullptr;
		bool dof_autofocus = true;
		//Camera pose at the last autofocus evaluation; refocusing is skipped while
		//it is unchanged. dof_refocus_pending forces one evaluation regardless
		//(startup, autofocus just re-enabled).
		HotBite::Engine::float3 dof_last_cam_pos{ 0.0f, 0.0f, 0.0f };
		HotBite::Engine::float3 dof_last_cam_dir{ 0.0f, 0.0f, 0.0f };
		bool dof_refocus_pending = true;

		void DrawMenuBar();
		void DrawDeleteRequest();
		void UpdateDofAutofocus();
	};

}
