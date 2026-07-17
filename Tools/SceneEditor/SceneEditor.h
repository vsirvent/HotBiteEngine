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

	// Shared, session-long editor state passed to every panel each frame. Owns the
	// bookkeeping needed to save the scene back out (see SceneSerializer.h).
	struct EditorState {
		HotBite::Engine::World* world = nullptr;

		std::string project_root;       // folder containing config.json
		std::string current_level_path; // absolute path to the currently open level.json

		HotBite::Engine::ECS::Entity selected_entity = HotBite::Engine::ECS::INVALID_ENTITY_ID;
		HotBite::Engine::float3 inspector_euler_degrees{ 0.0f, 0.0f, 0.0f };

		std::vector<PlacedInstance> placed_instances;
		std::set<HotBite::Engine::ECS::Entity> instance_entity_ids; // entities backed by placed_instances
		std::set<std::string> overridden_entities;                 // FBX-authored entities whose transform was edited

		std::vector<TemplateAsset> templates; // discovered/imported object templates
		std::string selected_template;        // template name chosen in the Asset Browser

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

		void DrawMenuBar();
	};

}
