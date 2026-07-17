#pragma once

#include "SceneEditor.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace HotBiteEditor {
	namespace EditorLayout {

		// Window titles docked by BeginDockspace. The panels' ImGui::Begin calls and
		// the DockBuilder layout below must agree on these names.
		inline constexpr const char* OUTLINER_WINDOW = "Outliner";
		inline constexpr const char* ASSET_BROWSER_WINDOW = "Asset Browser";
		inline constexpr const char* INSPECTOR_WINDOW = "Inspector";

		// Fullscreen dockspace over the main viewport's work area with a transparent
		// pass-through central node: the 3D scene (already rendered into the
		// backbuffer) shows through the middle while panels dock to the sides.
		// Layout persists via imgui.ini; the default (Outliner over Asset Browser on
		// the left, Inspector on the right) is built with DockBuilder the first time
		// no saved layout exists, or unconditionally on View/Reset Layout.
		inline void BeginDockspace(EditorState& state)
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(vp->WorkPos);
			ImGui::SetNextWindowSize(vp->WorkSize);
			ImGui::SetNextWindowViewport(vp->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
				ImGuiWindowFlags_NoBackground;
			ImGui::Begin("##EditorDockspaceHost", nullptr, host_flags);
			ImGui::PopStyleVar(3);

			ImGuiID dockspace_id = ImGui::GetID("EditorDockspace");
			bool has_layout = ImGui::DockBuilderGetNode(dockspace_id) != nullptr;
			if (!has_layout || state.apply_default_layout) {
				ImGui::DockBuilderRemoveNode(dockspace_id);
				ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
				ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

				ImGuiID center = dockspace_id;
				ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
				ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
				ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);

				ImGui::DockBuilderDockWindow(OUTLINER_WINDOW, left);
				ImGui::DockBuilderDockWindow(ASSET_BROWSER_WINDOW, left_bottom);
				ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW, right);
				ImGui::DockBuilderFinish(dockspace_id);
			}
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
			ImGui::End();
		}

		// The Project panel doubles as the pre-level project picker and stays a
		// floating window; center it the first time it appears (or on Reset Layout).
		inline void PlaceProject(const EditorState& state)
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(
				ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.30f, vp->WorkPos.y + vp->WorkSize.y * 0.20f), cond);
			ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.40f, vp->WorkSize.y * 0.50f), cond);
		}
	}
}
