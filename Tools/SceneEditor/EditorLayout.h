#pragma once

#include "SceneEditor.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace HotBiteEditor {
	namespace EditorLayout {

		// Window titles docked by BeginDockspace. The panels' ImGui::Begin calls and
		// the DockBuilder layout below must agree on these names.
		inline constexpr const char* OUTLINER_WINDOW = "Entities";
		inline constexpr const char* ASSET_BROWSER_WINDOW = "Asset Browser";
		inline constexpr const char* INSPECTOR_WINDOW = "Components";
		// The Materials panel is a floating window rather than a docked one: it is
		// opened for a material-authoring session and closed again, and it needs more
		// width than the side docks give it (thumbnail list plus a property column).
		inline constexpr const char* MATERIALS_WINDOW = "Materials";
		// The Templates panel is floating for the same reasons as the Materials one:
		// it is opened for an authoring session, and it needs a template list beside a
		// component column.
		inline constexpr const char* TEMPLATES_WINDOW = "Templates";
		// The Log panel is docked, unlike Materials/Templates: it is a full-width strip
		// at the bottom the way an IDE's console pane sits, meant to stay in view beside
		// whatever else is open rather than be brought forward for a session of its own.
		inline constexpr const char* LOG_WINDOW = "Log";

		// Fullscreen dockspace over the main viewport's work area with a transparent
		// pass-through central node: the 3D scene (already rendered into the
		// backbuffer) shows through the middle while panels dock to the sides.
		// Layout persists via imgui.ini; the default (Entities over Asset Browser on
		// the left, Components on the right) is built with DockBuilder the first time
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
			//An imgui.ini from before the Outliner->Entities / Inspector->Components
			//renames has a dock layout but no settings for the renamed windows, which
			//would leave them floating. Treat that as "no layout" and rebuild the
			//default docking; DockBuilderDockWindow below creates their settings
			//entries, so this fires at most once.
			bool panels_known = ImGui::FindWindowSettingsByID(ImHashStr(OUTLINER_WINDOW)) != nullptr &&
				ImGui::FindWindowSettingsByID(ImHashStr(INSPECTOR_WINDOW)) != nullptr;
			if (!has_layout || !panels_known || state.apply_default_layout) {
				ImGui::DockBuilderRemoveNode(dockspace_id);
				ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
				ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

				ImGuiID center = dockspace_id;
				//Split off the log strip first, from the *whole* dockspace, so it runs
				//the full width underneath the left/right docks rather than being boxed
				//in by them.
				ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
				ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
				ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
				ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);

				ImGui::DockBuilderDockWindow(OUTLINER_WINDOW, left);
				ImGui::DockBuilderDockWindow(ASSET_BROWSER_WINDOW, left_bottom);
				ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW, right);
				ImGui::DockBuilderDockWindow(LOG_WINDOW, bottom);
				ImGui::DockBuilderFinish(dockspace_id);
			}
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
			ImGui::End();
		}
	}
}
