#include "SceneEditor.h"
#include "ProjectBrowser.h"
#include "Outliner.h"
#include "Inspector.h"
#include "AssetBrowser.h"
#include "SceneSerializer.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "Engine.lib")

// imgui_impl_win32.h intentionally comments this declaration out (to avoid forcing a
// <windows.h> dependency on consumers that don't need it) and asks callers to copy it
// in directly, since Windows.h is already available here via Core/DXCore.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Systems;

namespace HotBiteEditor {

	SceneEditorApp::SceneEditorApp(HINSTANCE hInstance)
		: DXCore(hInstance, "HotBite Scene Editor", 1600, 900, true, true)
	{
		InitWindow();
		InitDirectX();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(wnd);
		ImGui_ImplDX11_Init(device, context);

		//The World's systems/coordinator are set up now, before any project/level is
		//chosen, so the ImGui panels have a valid (initially empty) Coordinator to
		//query from frame one.
		world.PreLoad(this);
		state.world = &world;

		//Start physics/audio/background ticking immediately, but with auto_render=false:
		//RenderSystem::Update() (Clear/Draw/Present) must not run before World::Init() has
		//prepared the vertex/BVH buffers, which only happens once a level is loaded. Until
		//then we drive our own minimal Clear+Present tick below so the ImGui project picker
		//still renders on an otherwise-empty window.
		world.Run(60, 60, 60, false);
		Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData&) {
			if (level_loaded) {
				world.GetSystem<RenderSystem>()->Update();
			}
			else {
				float color[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
				ClearScreen(color);
				Present();
			}
			return true;
			});
	}

	SceneEditorApp::~SceneEditorApp()
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void SceneEditorApp::ForwardWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
	}

	void SceneEditorApp::ClearScreen(const float color[4])
	{
		DXCore::ClearScreen(color);
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void SceneEditorApp::Present()
	{
		DrawMenuBar();
		ProjectBrowser::Draw(state, *this);
		if (level_loaded) {
			Outliner::Draw(state);
			Inspector::Draw(state);
			AssetBrowser::Draw(state);
		}

		ImGui::Render();
		ID3D11RenderTargetView* rtv = RenderTarget();
		context->OMSetRenderTargets(1, &rtv, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		DXCore::Present();
	}

	Coordinator* SceneEditorApp::GetCoordinator()
	{
		return world.GetCoordinator();
	}

	void SceneEditorApp::DrawMenuBar()
	{
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Save Level", nullptr, false, level_loaded)) {
					SceneSerializer::Save(state);
				}
				ImGui::EndMenu();
			}
			if (!state.status_message.empty()) {
				ImGui::TextUnformatted(state.status_message.c_str());
			}
			ImGui::EndMainMenuBar();
		}
	}

	void SceneEditorApp::OpenProject(const std::string& project_root)
	{
		state.project_root = project_root;
		state.status_message = "Project: " + project_root;
	}

	void SceneEditorApp::OpenLevel(const std::string& level_json_path)
	{
		if (level_loaded) {
			state.status_message = "A level is already open in this session; restart the editor to open a different one.";
			return;
		}
		if (!world.Load(level_json_path)) {
			state.status_message = "Failed to load level: " + level_json_path;
			return;
		}
		world.Init();
		// world.Run() already started in the constructor (with rendering disabled until
		// now); flipping level_loaded lets our own render tick switch to driving
		// RenderSystem::Update() instead of the bare Clear+Present used for the picker.
		state.current_level_path = level_json_path;
		level_loaded = true;
		state.status_message = "Loaded: " + level_json_path;
	}

	void SceneEditorApp::CloseLevel()
	{
		//Closing a level in-place is not supported for Milestone 1 (see OpenLevel) -
		//kept as a no-op hook for a future milestone that manages World lifetime per level.
	}

}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	// By default, a failed assert() in a debug CRT build pops a blocking modal dialog
	// (Abort/Retry/Ignore), which looks like a silent hang/crash when the process has
	// no visible console attached or is launched non-interactively. Route assert and
	// CRT error/warning reports to stderr instead so they show up in the log, and
	// disable stdio buffering so nothing printed just before a crash gets lost.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	_set_error_mode(_OUT_TO_STDERR);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	HotBiteEditor::SceneEditorApp app(hInstance);
	app.Run();
	return 0;
}
