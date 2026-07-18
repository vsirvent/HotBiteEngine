#include "SceneEditor.h"
#include "EditorLayout.h"
#include "ProjectBrowser.h"
#include "Outliner.h"
#include "Inspector.h"
#include "AssetBrowser.h"
#include "SceneSerializer.h"
#include "EditorAutomation.h"
#include "CrashHandler.h"
#include "RenderSettings.h"
#include "RenderDocIntegration.h"
#include "SelectionGizmo.h"

#include <Core/PostProcess.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <DirectXTex.h>
#include <shellapi.h>

#include <crtdbg.h>
#include <cmath>
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
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(wnd);
		ImGui_ImplDX11_Init(device, context);

		//The World's systems/coordinator are set up now, before any project/level is
		//chosen, so the ImGui panels have a valid (initially empty) Coordinator to
		//query from frame one.
		world.PreLoad(this);
		state.world = &world;
		//Input events fire from the same (main) thread the render tick runs on; the
		//camera controller is inert until a level provides a camera entity.
		editor_camera.Init(&world);

		//Start physics/audio/background ticking immediately, but with auto_render=false:
		//RenderSystem::Update() (Clear/Draw/Present) must not run before World::Init() has
		//prepared the vertex/BVH buffers, which only happens once a level is loaded. Until
		//then we drive our own minimal Clear+Present tick below so the ImGui project picker
		//still renders on an otherwise-empty window.
		world.Run(60, 60, 60, false);
		//The editor authors a scene, it doesn't play it: with the simulation live,
		//gravity re-settles every dynamic body (Ball, Cristal, ...) right after each
		//load and PhysicsSystem writes those body poses back over the Transforms, so
		//hand-placed positions drift and saves capture/restore the wrong spots -
		//which reads as "saving doesn't work". Edit/Simulate Physics re-enables it
		//for previewing.
		world.SetPhysicsPause(true);
		Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData& t) {
			//Remote-control commands run before the frame renders, so their effects
			//(and any screenshot taken at the end of this same frame) are consistent.
			EditorAutomation::ProcessCommands(state, *this);
			if (level_loaded) {
				editor_camera.Update((float)t.period / 1000000000.0f);
				UpdateDofAutofocus();
				world.GetSystem<RenderSystem>()->Update();
			}
			else {
				float color[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
				ClearScreen(color);
				Present();
			}
			return true;
			});

		//File: project/level lifecycle. Open/New are disabled once a level is
		//loaded because OpenLevel refuses a second level per session.
		menu_commands.push_back({ "File/New Project...",
			[this]() { return !level_loaded; },
			[this]() { ProjectBrowser::NewProjectWithDialog(state, *this); } });
		menu_commands.push_back({ "File/Open Level...",
			[this]() { return !level_loaded; },
			[this]() { ProjectBrowser::OpenLevelWithDialog(state, *this); } });
		menu_commands.push_back({ "File/Import Object...",
			[this]() { return level_loaded; },
			[this]() { AssetBrowser::ImportObjectWithDialog(state); } });
		menu_commands.push_back({ "File/Save Level",
			[this]() { return level_loaded; },
			[this]() { SceneSerializer::Save(state); } });
		menu_commands.push_back({ "File/Exit",
			nullptr,
			[this]() { Quit(); } });

		//Edit: physics preview. Off by default (see SetPhysicsPause above); while
		//checked, dynamic bodies simulate so the user can watch objects settle, then
		//pause again to keep authoring from the settled state.
		menu_commands.push_back({ "Edit/Simulate Physics",
			[this]() { return level_loaded; },
			[this]() { world.SetPhysicsPause(!world.GetPhysicsPause()); },
			[this]() { return !world.GetPhysicsPause(); } });

		//View: panel visibility toggles (the Project panel always shows before a
		//level loads, since it doubles as the project picker) and layout reset.
		menu_commands.push_back({ "View/Entities",
			[this]() { return level_loaded; },
			[this]() { state.show_outliner = !state.show_outliner; },
			[this]() { return state.show_outliner; } });
		menu_commands.push_back({ "View/Components",
			[this]() { return level_loaded; },
			[this]() { state.show_inspector = !state.show_inspector; },
			[this]() { return state.show_inspector; } });
		menu_commands.push_back({ "View/Asset Browser",
			[this]() { return level_loaded; },
			[this]() { state.show_asset_browser = !state.show_asset_browser; },
			[this]() { return state.show_asset_browser; } });
		menu_commands.push_back({ "View/Project",
			[this]() { return level_loaded; },
			[this]() { state.show_project = !state.show_project; },
			[this]() { return state.show_project; } });
		menu_commands.push_back({ "View/Reset Layout",
			nullptr,
			[this]() { state.apply_default_layout = true; } });
	}

	SceneEditorApp::~SceneEditorApp()
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		delete dof_effect;
		delete post_effect;
		delete gui;
	}

	Core::BaseDOFProcess* SceneEditorApp::GetDofEffect()
	{
		return dof_effect;
	}

	void SceneEditorApp::UpdateDofAutofocus()
	{
		if (!dof_autofocus || dof_effect == nullptr) {
			return;
		}
		auto camera_system = world.GetSystem<Systems::CameraSystem>();
		if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
			return;
		}
		const Components::Camera* cam = camera_system->GetCameras().GetData()[0].camera;

		//Refocus only when the camera actually moved (or the mode was just turned
		//on): the scene holds still while authoring, so the depth under the view
		//center can only change with the camera - and skipping the idle frames
		//keeps the scene raycast (which takes the physics lock) off the hot path.
		if (!dof_refocus_pending &&
			cam->world_position.x == dof_last_cam_pos.x &&
			cam->world_position.y == dof_last_cam_pos.y &&
			cam->world_position.z == dof_last_cam_pos.z &&
			cam->direction.x == dof_last_cam_dir.x &&
			cam->direction.y == dof_last_cam_dir.y &&
			cam->direction.z == dof_last_cam_dir.z) {
			return;
		}
		dof_last_cam_pos = cam->world_position;
		dof_last_cam_dir = cam->direction;
		dof_refocus_pending = false;

		//Focal distance = depth of whatever sits at the center of the view, from
		//the same collider/AABB raycast a viewport click uses. Marbles refocuses on
		//its one subject (the player ball); the editor's subject is whatever the
		//user aimed the camera at.
		Coordinator* c = world.GetCoordinator();
		if (c == nullptr) {
			return;
		}
		float distance = 0.0f;
		if (SelectionGizmo::RaycastScene(c, cam->world_position, cam->direction, &distance) == INVALID_ENTITY_ID) {
			//Nothing under the view center (sky): fall back to the orbit target,
			//the point the camera controls revolve around.
			EditorCamera::Pose pose;
			if (!editor_camera.GetPose(pose)) {
				return;
			}
			float3 d = { pose.target.x - cam->world_position.x,
						 pose.target.y - cam->world_position.y,
						 pose.target.z - cam->world_position.z };
			distance = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
		}
		dof_effect->SetFocus(distance);

		//Marbles' distance-based aperture (MarblesGame::UpdateCamera): amplitude
		//(1 - distance/20) * 10, clamped at 0. Up close that's a wide-open macro
		//lens with a paper-thin depth of field; past 20 units the aperture is fully
		//stopped down and the whole scene renders in focus, so backing the camera
		//away never leaves the view blurred.
		float amplitude = (1.0f - distance / 20.0f) * 10.0f;
		if (amplitude < 0.0f) {
			amplitude = 0.0f;
		}
		dof_effect->SetAmplitude(amplitude);
	}

	void SceneEditorApp::ForwardWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		//When the user resizes the window, the fixed-size backbuffer is stretched
		//to the client area on present. ImGui is laid out in backbuffer pixels
		//(see ClearScreen), so remap mouse positions from client to backbuffer
		//space; the UI, the 3D scene and the cursor then stay in agreement at any
		//window size. Only WM_MOUSEMOVE carries coordinates the ImGui backend uses.
		if (uMsg == WM_MOUSEMOVE) {
			RECT rc;
			if (GetClientRect(hWnd, &rc)) {
				const float cw = (float)(rc.right - rc.left);
				const float ch = (float)(rc.bottom - rc.top);
				if (cw > 0.0f && ch > 0.0f &&
					((int)cw != GetWidth() || (int)ch != GetHeight())) {
					//Coordinates are signed: during a captured drag they can go
					//negative or past the client edge.
					const int x = (int)std::lroundf((float)(short)LOWORD(lParam) * (float)GetWidth() / cw);
					const int y = (int)std::lroundf((float)(short)HIWORD(lParam) * (float)GetHeight() / ch);
					lParam = (LPARAM)(((y & 0xFFFF) << 16) | (x & 0xFFFF));
				}
			}
		}
		ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
	}

	void SceneEditorApp::ClearScreen(const float color[4])
	{
		DXCore::ClearScreen(color);
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		//ImGui must think in backbuffer pixels, not client pixels: the engine
		//renders at the swapchain size and the whole backbuffer is stretched to
		//the client area on present. The win32 backend just set DisplaySize to
		//the client rect; force it back to the backbuffer size (mouse input is
		//remapped correspondingly in ForwardWindowMessage).
		ImGui::GetIO().DisplaySize = ImVec2((float)GetWidth(), (float)GetHeight());
		ImGui::NewFrame();
	}

	void SceneEditorApp::Present()
	{
		DrawMenuBar();
		if (!level_loaded) {
			//Pre-level, the Project panel is the project/level picker.
			ProjectBrowser::Draw(state, *this);
		}
		else {
			//The dockspace must be submitted before any window that docks into it.
			EditorLayout::BeginDockspace(state);
			if (state.show_project) {
				ProjectBrowser::Draw(state, *this);
			}
			if (state.show_outliner) {
				Outliner::Draw(state, editor_camera);
			}
			if (state.show_inspector) {
				Inspector::Draw(state);
			}
			if (state.show_asset_browser) {
				AssetBrowser::Draw(state);
			}
			SelectionGizmo::Draw(state);
		}
		//A View/Reset Layout request has now been consumed by every visible panel.
		state.apply_default_layout = false;

		ImGui::Render();
		ID3D11RenderTargetView* rtv = RenderTarget();
		context->OMSetRenderTargets(1, &rtv, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		//The backbuffer now holds the complete frame (scene + UI): let automation
		//capture pending screenshots and flush its responses before presenting.
		EditorAutomation::OnFrameEnd(state, *this);

		DXCore::Present();
	}

	Coordinator* SceneEditorApp::GetCoordinator()
	{
		return world.GetCoordinator();
	}

	//Splits a MenuCommand path ("File/Save Level") into its menu and item parts.
	static void SplitMenuPath(const std::string& path, std::string& menu, std::string& item)
	{
		size_t slash = path.find('/');
		if (slash == std::string::npos) {
			menu = path;
			item.clear();
		}
		else {
			menu = path.substr(0, slash);
			item = path.substr(slash + 1);
		}
	}

	void SceneEditorApp::DrawMenuBar()
	{
		if (ImGui::BeginMainMenuBar()) {
			//Commands are grouped into top-level menus by the prefix of their path;
			//consecutive commands sharing a prefix land in the same BeginMenu block.
			for (size_t i = 0; i < menu_commands.size();) {
				std::string menu, item;
				SplitMenuPath(menu_commands[i].path, menu, item);
				if (ImGui::BeginMenu(menu.c_str())) {
					for (; i < menu_commands.size(); ++i) {
						std::string m, it;
						SplitMenuPath(menu_commands[i].path, m, it);
						if (m != menu) {
							break;
						}
						MenuCommand& mc = menu_commands[i];
						bool is_enabled = !mc.enabled || mc.enabled();
						bool is_checked = mc.checked && mc.checked();
						if (ImGui::MenuItem(it.c_str(), nullptr, is_checked, is_enabled)) {
							mc.action();
						}
					}
					ImGui::EndMenu();
				}
				else {
					//Menu closed: still advance past this group.
					std::string m, it;
					for (; i < menu_commands.size(); ++i) {
						SplitMenuPath(menu_commands[i].path, m, it);
						if (m != menu) {
							break;
						}
					}
				}
			}
			RenderSettings::DrawMenu(*this);
			if (!state.status_message.empty()) {
				ImGui::TextUnformatted(state.status_message.c_str());
			}
			ImGui::EndMainMenuBar();
		}
	}

	bool SceneEditorApp::ExecuteMenuCommand(const std::string& path, std::string& error)
	{
		for (auto& mc : menu_commands) {
			if (mc.path == path) {
				if (mc.enabled && !mc.enabled()) {
					error = "menu command is disabled: " + path;
					return false;
				}
				mc.action();
				return true;
			}
		}
		error = "unknown menu command: " + path;
		return false;
	}

	bool SceneEditorApp::CaptureBackBuffer(const std::string& png_path, std::string& error)
	{
		//WIC (used by SaveToWICFile) needs COM on this thread; nothing else on the
		//render thread initializes it. RPC_E_CHANGED_MODE just means it already was.
		static bool com_initialized = false;
		if (!com_initialized) {
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			com_initialized = true;
		}

		char hr_text[32];
		ID3D11Texture2D* back_buffer = nullptr;
		HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
		if (FAILED(hr)) {
			snprintf(hr_text, sizeof(hr_text), "0x%08X", (unsigned)hr);
			error = std::string("GetBuffer failed: ") + hr_text;
			return false;
		}
		DirectX::ScratchImage image;
		hr = DirectX::CaptureTexture(device, context, back_buffer, image);
		back_buffer->Release();
		if (FAILED(hr)) {
			snprintf(hr_text, sizeof(hr_text), "0x%08X", (unsigned)hr);
			error = std::string("CaptureTexture failed: ") + hr_text;
			return false;
		}
		std::wstring wpath(png_path.begin(), png_path.end());
		hr = DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wpath.c_str());
		if (FAILED(hr)) {
			snprintf(hr_text, sizeof(hr_text), "0x%08X", (unsigned)hr);
			error = std::string("SaveToWICFile failed: ") + hr_text + " (" + png_path + ")";
			return false;
		}
		return true;
	}

	void SceneEditorApp::OpenProject(const std::string& project_root)
	{
		state.project_root = project_root;
		state.status_message = "Project: " + project_root;
	}

	bool SceneEditorApp::OpenLevel(const std::string& level_json_path)
	{
		if (level_loaded) {
			state.status_message = "A level is already open in this session; restart the editor to open a different one.";
			return false;
		}
		if (!world.Load(level_json_path)) {
			state.status_message = "Failed to load level: " + level_json_path;
			return false;
		}
		world.Init();
		SceneSerializer::LoadEditorData(state, level_json_path);

		//Install the full post-process pipeline, mirroring Marbles' setup
		//(MainEffect -> DOF -> backbuffer; RenderSystem finds the DOF stage by
		//walking the chain). Without a pipeline the RenderSystem never runs the
		//deferred light mix / ray tracing / AA / motion blur and the scene
		//presents as a flat base pass.
		post_effect = new Core::MainEffect(context, width, height);
		gui = new UI::GUI(context, width, height, world.GetCoordinator());
		dof_effect = new Core::DOFBokeProcess(context, width, height, world.GetCoordinator());
		post_effect->SetNext(dof_effect);
		dof_effect->SetEnabled(true);
		dof_effect->SetFocus(30.0f);
		dof_effect->SetAmplitude(5.0f);
		dof_effect->SetNext(gui);
		world.SetPostProcessPipeline(post_effect);
		RenderSettings::ApplyHighDefaults(*this);

		// world.Run() already started in the constructor (with rendering disabled until
		// now); flipping level_loaded lets our own render tick switch to driving
		// RenderSystem::Update() instead of the bare Clear+Present used for the picker.
		state.current_level_path = level_json_path;
		level_loaded = true;
		state.status_message = "Loaded: " + level_json_path;
		return true;
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
#if 0
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
#endif
	// Supported switches:
	//   --project <dir>      open a project root (folder containing config.json)
	//   --level <level.json> open a level directly (project root derived if not given)
	//   --automation <dir>   enable the file-based remote-control channel (see
	//                        EditorAutomation.h) rooted at <dir>
	//   --renderdoc [dll]    load the RenderDoc in-app API for programmatic frame
	//                        captures (rdoc_capture automation command); optional
	//                        value overrides the default renderdoc.dll path
	std::string project_arg, level_arg, automation_arg;
	bool renderdoc_enabled = false;
	std::string renderdoc_dll;
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		auto narrow = [](LPCWSTR w) {
			int len = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
			std::string s(len > 0 ? len - 1 : 0, '\0');
			if (len > 1) {
				WideCharToMultiByte(CP_ACP, 0, w, -1, s.data(), len, nullptr, nullptr);
			}
			return s;
		};
		if (argv != nullptr) {
			for (int i = 1; i < argc; ++i) {
				std::string key = narrow(argv[i]);
				if (key == "--project" && i + 1 < argc) {
					project_arg = narrow(argv[++i]);
				}
				else if (key == "--level" && i + 1 < argc) {
					level_arg = narrow(argv[++i]);
				}
				else if (key == "--automation" && i + 1 < argc) {
					automation_arg = narrow(argv[++i]);
				}
				else if (key == "--renderdoc") {
					renderdoc_enabled = true;
					//Optional value: a custom renderdoc.dll path (not another switch).
					if (i + 1 < argc) {
						std::string value = narrow(argv[i + 1]);
						if (value.rfind("--", 0) != 0) {
							renderdoc_dll = value;
							++i;
						}
					}
				}
			}
			LocalFree(argv);
		}
	}

	if (!automation_arg.empty()) {
		HotBiteEditor::EditorAutomation::Init(automation_arg);
	}

	//Crash reports land in the automation dir when one is set (so a driving agent
	//finds crash.txt/crash.dmp next to its command channel), else beside the exe.
	{
		std::string crash_dir = automation_arg;
		if (crash_dir.empty()) {
			char exe_path[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
			crash_dir = exe_path;
			size_t slash = crash_dir.find_last_of('\\');
			crash_dir = (slash != std::string::npos) ? crash_dir.substr(0, slash) : ".";
		}
		HotBiteEditor::CrashHandler::Install(crash_dir);

		//RenderDoc's dll must be loaded before the D3D11 device exists (created in
		//the SceneEditorApp constructor below), or the device escapes its hooks.
		if (renderdoc_enabled) {
			if (!HotBiteEditor::RenderDocIntegration::Load(renderdoc_dll, crash_dir)) {
				fprintf(stderr, "RenderDoc integration requested (--renderdoc) but loading the API failed.\n");
			}
		}
	}

	HotBiteEditor::SceneEditorApp app(hInstance);
	if (!project_arg.empty()) {
		app.OpenProject(project_arg);
	}
	if (!level_arg.empty()) {
		if (app.GetState().project_root.empty()) {
			app.GetState().project_root = HotBiteEditor::ProjectBrowser::DeriveProjectRoot(level_arg);
		}
		app.OpenLevel(level_arg);
	}
	app.Run();
	return 0;
}
