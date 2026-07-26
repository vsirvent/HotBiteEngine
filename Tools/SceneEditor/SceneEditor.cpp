#include "SceneEditor.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "ProjectBrowser.h"
#include "Outliner.h"
#include "Inspector.h"
#include "AssetBrowser.h"
#include "MaterialPanel.h"
#include "MaterialPreview.h"
#include "TemplatePanel.h"
#include "EntityOps.h"
#include "SceneSerializer.h"
#include "EditorAutomation.h"
#include "CrashHandler.h"
#include "RenderSettings.h"
#include "RenderDocIntegration.h"
#include "SelectionGizmo.h"
#include "Selection.h"
#include "PhysicsDebug.h"
#include "PhysicsPreview.h"

#include <Core/PostProcess.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <DirectXTex.h>
#include <shellapi.h>

#include <crtdbg.h>
#include <cfloat>
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
		//Open filling the screen. The 1600x900 above is only the fallback for a
		//display too small to maximize into; the editor's docked panels (the
		//Components panel in particular) are taller than that, so a fixed window
		//pushes their lower half - including the Add Component button - past the
		//bottom edge where it cannot be reached.
		SetStartMaximized(true);
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
		//then we drive our own minimal Clear+Present tick below so the menu bar (File >
		//Open Level..., the only thing on screen pre-level) still renders.
		world.Run(60, 60, 60, false);
		//The editor authors a scene, it doesn't play it: with the simulation live,
		//gravity re-settles every dynamic body (Ball, Cristal, ...) right after each
		//load and PhysicsSystem writes those body poses back over the Transforms, so
		//hand-placed positions drift and saves capture/restore the wrong spots -
		//which reads as "saving doesn't work". Edit/Simulate Physics re-enables it
		//for previewing.
		world.SetPhysicsPause(true);
		Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData& t) {
			//A level queued from UI code (the File menu) is loaded here, outside of any
			//ImGui frame: OpenLevel paints its own progress frames while it blocks.
			if (!pending_level_path.empty()) {
				std::string path = pending_level_path;
				pending_level_path.clear();
				OpenLevel(path);
			}
			//Remote-control commands run before the frame renders, so their effects
			//(and any screenshot taken at the end of this same frame) are consistent.
			EditorAutomation::ProcessCommands(state, *this);
			if (level_loaded) {
				editor_camera.Update((float)t.period / 1000000000.0f);
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
		//Importing means importing a *template* now: an object worth placing is a
		//template, and the meshes one points at come from the level's own FBX assets
		//rather than from importing an FBX as an object of its own.
		menu_commands.push_back({ "File/Import Template...",
			[this]() { return level_loaded; },
			[this]() { TemplateOps::ImportTemplateWithDialog(state); } });
		menu_commands.push_back({ "File/Save Level",
			[this]() { return level_loaded; },
			[this]() { SceneSerializer::Save(state); } });
		//Materials live in .mat files shared between levels, so they save separately
		//from the level (see MaterialPanel.h). Enabled only when something is dirty.
		menu_commands.push_back({ "File/Save Materials",
			[this]() { return level_loaded && MaterialOps::HasUnsavedMaterials(state); },
			[this]() {
				std::string error;
				if (!MaterialOps::SaveMaterials(state, error)) {
					state.status_message = "Save materials failed: " + error;
				}
			} });
		//Authored templates are .tpl files shared between levels, saved like materials
		//(see TemplatePanel.h). Unlike materials, saving the *level* flushes these too
		//- the level's "templates" array names the .tpl file, so writing that reference
		//while the file did not exist yet would produce a level that cannot reload.
		menu_commands.push_back({ "File/Save Templates",
			[this]() { return level_loaded && TemplateOps::HasUnsavedTemplates(state); },
			[this]() {
				std::string error;
				if (!TemplateOps::SaveTemplates(state, error)) {
					state.status_message = "Save templates failed: " + error;
				}
			} });
		menu_commands.push_back({ "File/Exit",
			nullptr,
			[this]() { Quit(); } });

		//Edit: undo/redo of scene edits (see EditorHistory.h for what records and
		//the rule every new command must follow). Also on Ctrl+Z / Ctrl+Y /
		//Ctrl+Shift+Z (see Present) and the automation `undo`/`redo` commands.
		menu_commands.push_back({ "Edit/Undo",
			[this]() { return level_loaded && EditorHistory::CanUndo(); },
			[this]() { std::string err; EditorHistory::Undo(state, err); } });
		menu_commands.push_back({ "Edit/Redo",
			[this]() { return level_loaded && EditorHistory::CanRedo(); },
			[this]() { std::string err; EditorHistory::Redo(state, err); } });

		//Edit: entity clipboard. Copy/Cut act on the selection, Paste on the
		//clipboard; all three also on Ctrl+C / Ctrl+X / Ctrl+V (see Present) and the
		//automation copy/cut/paste commands. A cut clones-then-hides its source so
		//paste and undo still work (see EntityOps.h).
		menu_commands.push_back({ "Edit/Copy",
			[this]() { return level_loaded && EntityOps::CanCopySelected(state); },
			[this]() { std::string err; if (!EntityOps::CopySelected(state, err)) { state.status_message = "Copy failed: " + err; } } });
		menu_commands.push_back({ "Edit/Cut",
			[this]() { return level_loaded && EntityOps::CanCopySelected(state); },
			[this]() { std::string err; if (!EntityOps::CutSelected(state, err)) { state.status_message = "Cut failed: " + err; } } });
		menu_commands.push_back({ "Edit/Paste",
			[this]() { return level_loaded && state.clipboard.kind != EntityClipboard::Kind::None; },
			[this]() { std::string err; if (!EntityOps::Paste(state, err)) { state.status_message = "Paste failed: " + err; } } });

		//Edit: delete the selection (also on the Del key and in the Entities panel's
		//context menu). Everything routes through delete_requested so a multi-entity
		//delete gets the same confirmation whichever surface asked for it.
		menu_commands.push_back({ "Edit/Delete",
			[this]() { return level_loaded && EntityOps::CanDeleteSelected(state); },
			[this]() { state.delete_requested = true; } });

		//Edit: turn the selected entity into a placeable template. The naming happens
		//in the Templates panel's modal, so this opens the panel with the request
		//pending rather than inventing a name of its own.
		menu_commands.push_back({ "Edit/Create Template from Selection",
			[this]() {
				Coordinator* c = world.GetCoordinator();
				return level_loaded && c != nullptr &&
					state.selected_entity != INVALID_ENTITY_ID &&
					c->ContainsComponent<Components::Mesh>(state.selected_entity);
			},
			[this]() {
				state.show_template_panel = true;
				TemplatePanel::RequestTemplateFromSelection(state);
			} });

		//Edit: physics preview. Off by default (see SetPhysicsPause above); while
		//checked, dynamic bodies simulate so the user can watch objects settle.
		//Switching it back off rewinds them to the transforms they were authored
		//with - the simulation is a preview, not an edit (see PhysicsPreview.h).
		menu_commands.push_back({ "Edit/Simulate Physics",
			[this]() { return level_loaded; },
			[this]() { PhysicsPreview::SetEnabled(state, !PhysicsPreview::IsEnabled(state)); },
			[this]() { return PhysicsPreview::IsEnabled(state); } });

		//Edit: viewport gizmo tool. Also on the 1/2/3 keys (see SelectionGizmo::Draw);
		//W/E/R would collide with the camera fly keys.
		menu_commands.push_back({ "Edit/Gizmo: Translate",
			[this]() { return level_loaded; },
			[this]() { state.gizmo_mode = GizmoMode::Translate; },
			[this]() { return state.gizmo_mode == GizmoMode::Translate; } });
		menu_commands.push_back({ "Edit/Gizmo: Rotate",
			[this]() { return level_loaded; },
			[this]() { state.gizmo_mode = GizmoMode::Rotate; },
			[this]() { return state.gizmo_mode == GizmoMode::Rotate; } });
		menu_commands.push_back({ "Edit/Gizmo: Scale",
			[this]() { return level_loaded; },
			[this]() { state.gizmo_mode = GizmoMode::Scale; },
			[this]() { return state.gizmo_mode == GizmoMode::Scale; } });

		//View: panel visibility toggles and layout reset. All of them need a level:
		//before one is open the editor is just the menu bar over an empty viewport.
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
		menu_commands.push_back({ "View/Materials",
			[this]() { return level_loaded; },
			[this]() { state.show_material_panel = !state.show_material_panel; },
			[this]() { return state.show_material_panel; } });
		menu_commands.push_back({ "View/Templates",
			[this]() { return level_loaded; },
			[this]() { state.show_template_panel = !state.show_template_panel; },
			[this]() { return state.show_template_panel; } });
		//View: physics collider wireframes (see PhysicsDebug.h). Two entries acting
		//as a radio group - clicking the active one turns the overlay off - because
		//"all" is expensive enough on a terrain-heavy scene to want the selection-only
		//mode as the everyday choice.
		menu_commands.push_back({ "View/Colliders: Selection",
			[this]() { return level_loaded; },
			[this]() {
				state.collider_view = (state.collider_view == ColliderView::Selection)
					? ColliderView::Off : ColliderView::Selection;
			},
			[this]() { return state.collider_view == ColliderView::Selection; } });
		menu_commands.push_back({ "View/Colliders: All",
			[this]() { return level_loaded; },
			[this]() {
				state.collider_view = (state.collider_view == ColliderView::All)
					? ColliderView::Off : ColliderView::All;
			},
			[this]() { return state.collider_view == ColliderView::All; } });

		menu_commands.push_back({ "View/Reset Layout",
			nullptr,
			[this]() { state.apply_default_layout = true; } });
	}

	SceneEditorApp::~SceneEditorApp()
	{
		//Before the ImGui backend goes away: the material thumbnails are D3D textures
		//ImGui is still holding texture ids for.
		MaterialPreview::Shutdown();
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		delete dof_effect;
		delete lens_effect;
		delete post_effect;
		delete gui;
	}

	Core::BaseDOFProcess* SceneEditorApp::GetDofEffect()
	{
		return dof_effect;
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
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)GetWidth(), (float)GetHeight());
		//Loading paints several frames inside a single tick (see RenderLoadingFrame),
		//which can leave the win32 backend measuring a zero delta between two of them -
		//ImGui asserts on that ("Need a positive DeltaTime!").
		if (io.DeltaTime <= 0.0f) {
			io.DeltaTime = 1.0f / 60.0f;
		}
		ImGui::NewFrame();
	}

	void SceneEditorApp::Present()
	{
		DrawMenuBar();
		if (level_loaded) {
			//Undo/redo hotkeys, gated like the gizmo's 1/2/3 keys: inert while a
			//text field owns the keyboard. Ctrl+Shift+Z is the usual redo alias.
			ImGuiIO& io = ImGui::GetIO();
			//Entities can disappear behind the selection's back (an undo that
			//destroys a pasted clone, say); drop those before any panel reads it.
			Selection::Prune(state);
			//Del deletes the selection, with the same gating as the other hotkeys so
			//it never fires while a name is being typed into a text field.
			if (!io.WantTextInput && !io.KeyCtrl &&
				ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
				state.delete_requested = true;
			}
			if (!io.WantTextInput && io.KeyCtrl) {
				std::string err;
				if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
					io.KeyShift ? EditorHistory::Redo(state, err) : EditorHistory::Undo(state, err);
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
					EditorHistory::Redo(state, err);
				}
				//Entity clipboard, same keys as the Edit menu. Failures land in the
				//status message so the shortcut is never silently inert.
				else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
					if (EntityOps::CanCopySelected(state) && !EntityOps::CopySelected(state, err)) {
						state.status_message = "Copy failed: " + err;
					}
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
					if (EntityOps::CanCopySelected(state) && !EntityOps::CutSelected(state, err)) {
						state.status_message = "Cut failed: " + err;
					}
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
					if (state.clipboard.kind != EntityClipboard::Kind::None && !EntityOps::Paste(state, err)) {
						state.status_message = "Paste failed: " + err;
					}
				}
			}
			//The dockspace must be submitted before any window that docks into it.
			EditorLayout::BeginDockspace(state);
			if (state.show_outliner) {
				Outliner::Draw(state, editor_camera);
			}
			if (state.show_inspector) {
				Inspector::Draw(state);
			}
			if (state.show_asset_browser) {
				AssetBrowser::Draw(state);
			}
			if (state.show_material_panel) {
				MaterialPanel::Draw(state);
			}
			if (state.show_template_panel) {
				TemplatePanel::Draw(state);
			}
			//Under the gizmo, so the selection handles stay readable on top of a
			//dense collider wireframe.
			PhysicsDebug::Draw(state);
			SelectionGizmo::Draw(state);
			DrawDeleteRequest();
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

	//Consumes a pending delete request (Del key or the Entities panel's context
	//menu). A single entity goes straight away - it is one Ctrl+Z from coming
	//back - while deleting several asks first, since losing a whole selection by
	//accident is the expensive mistake. The modal lives here, at window scope,
	//because ImGui popups cannot be opened from inside the transient popup or the
	//key handler that requested them.
	void SceneEditorApp::DrawDeleteRequest()
	{
		static constexpr const char* CONFIRM_POPUP = "Delete entities?";
		if (state.delete_requested) {
			state.delete_requested = false;
			if (!EntityOps::CanDeleteSelected(state)) {
				state.status_message = "Nothing in the selection can be deleted.";
			}
			else if (Selection::Count(state) > 1) {
				ImGui::OpenPopup(CONFIRM_POPUP);
			}
			else {
				std::string err;
				if (!EntityOps::DeleteSelected(state, err)) {
					state.status_message = "Delete failed: " + err;
				}
			}
		}
		//Centered like a standard confirmation; Enter confirms, Esc/Cancel backs out.
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(CONFIRM_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete %d selected entities?", (int)Selection::Count(state));
			ImGui::TextUnformatted("This can be undone with Ctrl+Z.");
			ImGui::Separator();
			bool confirm = ImGui::Button("Delete", ImVec2(90.0f, 0.0f));
			confirm |= ImGui::IsKeyPressed(ImGuiKey_Enter, false);
			ImGui::SameLine();
			bool cancel = ImGui::Button("Cancel", ImVec2(90.0f, 0.0f));
			cancel |= ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			if (confirm) {
				std::string err;
				if (!EntityOps::DeleteSelected(state, err)) {
					state.status_message = "Delete failed: " + err;
				}
				ImGui::CloseCurrentPopup();
			}
			else if (cancel) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
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

	//Paints the loading overlay: a bare Clear + one ImGui frame + Present, driven from
	//inside the load rather than from the render tick, because the load owns the thread
	//that would otherwise be drawing. Balanced (it opens and closes its own frame), so
	//the tick that called into the load continues normally afterwards.
	void SceneEditorApp::RenderLoadingFrame()
	{
		//Keep the window alive while a long load blocks the message loop: without this
		//Windows declares it unresponsive and covers our frames with a ghost copy.
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				//Not ours to consume - hand it back to DXCore::Run's loop.
				PostQuitMessage((int)msg.wParam);
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		float color[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
		ClearScreen(color);

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		//A zero height auto-fits the content; the width is fixed so the bar does not
		//jump around as the stage labels change length.
		ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Always);
		if (ImGui::Begin("Loading level", nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
			ImGui::TextUnformatted(loading_level.c_str());
			ImGui::Spacing();
			char overlay[32];
			snprintf(overlay, sizeof(overlay), "%.0f%%", loading_progress * 100.0f);
			ImGui::ProgressBar(loading_progress, ImVec2(-FLT_MIN, 0.0f), overlay);
			ImGui::Spacing();
			ImGui::TextDisabled("%s", loading_stage.c_str());
		}
		ImGui::End();

		ImGui::Render();
		ID3D11RenderTargetView* rtv = RenderTarget();
		context->OMSetRenderTargets(1, &rtv, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		DXCore::Present();
	}

	void SceneEditorApp::ShowLoadingProgress(float fraction, const std::string& stage)
	{
		loading_progress = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
		loading_stage = stage;
		RenderLoadingFrame();
	}

	void SceneEditorApp::RequestOpenLevel(const std::string& level_json_path)
	{
		pending_level_path = level_json_path;
	}

	bool SceneEditorApp::OpenLevel(const std::string& level_json_path)
	{
		if (level_loaded) {
			state.status_message = "A level is already open in this session; restart the editor to open a different one.";
			return false;
		}

		//World::Load reports its progress through the callback the demo game passes it
		//(World.h: Load(file, progress, OnLoadProgress, unit)). The accumulator it feeds
		//adds up to LOAD_UNITS across eight phases, in this order; the callback fires
		//*after* each one, so the label attached to a value names what comes next.
		static constexpr float LOAD_UNITS = 70.0f;
		//Share of the bar the engine load owns; the rest covers the editor-side steps
		//below it (World::Init, editor data, post-process pipeline).
		static constexpr float LOAD_SHARE = 0.8f;
		auto stage_after = [](float units) -> const char* {
			if (units < 5.0f)  { return "Reading level file..."; }
			if (units < 10.0f) { return "Loading scene geometry..."; }
			if (units < 20.0f) { return "Loading templates..."; }
			if (units < 30.0f) { return "Loading materials..."; }
			if (units < 40.0f) { return "Loading meshes, instances and sky..."; }
			if (units < 50.0f) { return "Loading lights..."; }
			if (units < 60.0f) { return "Loading entities..."; }
			return "Loading audio...";
			};

		loading_level = level_json_path;
		//One frame at 0% so the overlay is on screen before the first (potentially very
		//long) phase - reading the level file and its FBX - begins.
		ShowLoadingProgress(0.0f, stage_after(0.0f));

		float units = 0.0f;
		bool loaded = world.Load(level_json_path, &units,
			[this, &stage_after](float done) {
				ShowLoadingProgress(LOAD_SHARE * done / LOAD_UNITS, stage_after(done));
			}, 1.0f);
		if (!loaded) {
			state.status_message = "Failed to load level: " + level_json_path;
			return false;
		}
		ShowLoadingProgress(LOAD_SHARE, "Preparing scene buffers...");
		world.Init();
		ShowLoadingProgress(0.9f, "Restoring editor data...");
		SceneSerializer::LoadEditorData(state, level_json_path);
		ShowLoadingProgress(0.95f, "Building render pipeline...");

		//Install the full post-process pipeline, mirroring Marbles' setup
		//(MainEffect -> DOF -> Lens -> GUI -> backbuffer; RenderSystem finds the DOF
		//and lens stages by walking the chain). Without a pipeline the RenderSystem
		//never runs the deferred light mix / ray tracing / AA / motion blur and the
		//scene presents as a flat base pass.
		post_effect = new Core::MainEffect(context, width, height);
		gui = new UI::GUI(context, width, height, world.GetCoordinator());
		dof_effect = new Core::DOFBokeProcess(context, width, height, world.GetCoordinator());
		post_effect->SetNext(dof_effect);
		dof_effect->SetEnabled(true);
		dof_effect->SetFocus(30.0f);
		dof_effect->SetAmplitude(5.0f);
		//Camera artifacts go after the lens has focused the image and before the GUI,
		//so the editor's own interface stays sharp, steady and unshaded no matter how
		//far the sliders are pushed.
		lens_effect = new Core::LensEffect(context, width, height, world.GetCoordinator());
		dof_effect->SetNext(lens_effect);
		lens_effect->SetNext(gui);
		world.SetPostProcessPipeline(post_effect);
		RenderSettings::ApplyHighDefaults(*this);

		// world.Run() already started in the constructor (with rendering disabled until
		// now); flipping level_loaded lets our own render tick switch to driving
		// RenderSystem::Update() instead of the bare Clear+Present used for the picker.
		state.current_level_path = level_json_path;
		level_loaded = true;
		//A fresh level starts with an empty edit history.
		EditorHistory::Clear();
		//Last overlay frame at 100%, then back to the normal render tick, which now
		//draws the scene instead.
		ShowLoadingProgress(1.0f, "Ready");
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
