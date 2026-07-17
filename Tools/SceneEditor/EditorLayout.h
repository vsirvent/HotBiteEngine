#pragma once

#include "SceneEditor.h"
#include "imgui.h"

namespace HotBiteEditor {
	namespace EditorLayout {

		// Positions/sizes the next ImGui window as a fraction of the main viewport's
		// work area (the space below the menu bar). Applied only the first time a
		// window is ever shown (ImGuiCond_FirstUseEver), so user rearrangements
		// persist via imgui.ini; View/Reset Layout re-applies it unconditionally.
		inline void PlaceWindow(const EditorState& state,
			float x_frac, float y_frac, float w_frac, float h_frac)
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(
				ImVec2(vp->WorkPos.x + vp->WorkSize.x * x_frac, vp->WorkPos.y + vp->WorkSize.y * y_frac), cond);
			ImGui::SetNextWindowSize(
				ImVec2(vp->WorkSize.x * w_frac, vp->WorkSize.y * h_frac), cond);
		}

		// The editor's default panel arrangement: Outliner above the Asset Browser in
		// a left column, Inspector as a right column, Project as a centered window.
		inline void PlaceOutliner(const EditorState& state) { PlaceWindow(state, 0.0f, 0.0f, 0.16f, 0.55f); }
		inline void PlaceAssetBrowser(const EditorState& state) { PlaceWindow(state, 0.0f, 0.55f, 0.16f, 0.45f); }
		inline void PlaceInspector(const EditorState& state) { PlaceWindow(state, 0.81f, 0.0f, 0.19f, 0.50f); }
		inline void PlaceProject(const EditorState& state) { PlaceWindow(state, 0.30f, 0.20f, 0.40f, 0.50f); }
	}
}
