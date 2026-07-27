#include "ShadowDebug.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Lights.h>
#include <string>
#include <vector>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace ShadowDebug {

		//These MUST match CASCADE_DEBUG_COLOR in Shaders/Common/PixelFunctions.hlsli
		//(and its copy in SimpleLight.hlsli). The shader tints the scene; this only
		//names the tints, and a legend that disagrees with what is on screen is worse
		//than no legend at all.
		static const ImVec4 CASCADE_COLOR[MAX_DRAWN_CASCADES] = {
			ImVec4(0.15f, 1.00f, 0.25f, 1.0f), //0 - nearest, tightest
			ImVec4(1.00f, 0.85f, 0.10f, 1.0f),
			ImVec4(1.00f, 0.35f, 0.05f, 1.0f),
			ImVec4(1.00f, 0.15f, 0.75f, 1.0f), //3 - farthest, coarsest
		};
		static const ImVec4 OUTSIDE_COLOR = ImVec4(0.20f, 0.35f, 1.00f, 1.0f);
		//...and these MUST match STATIC_DEBUG_* in the same shader headers.
		static const ImVec4 STATIC_OUTSIDE_COLOR = ImVec4(1.00f, 0.12f, 0.12f, 1.0f);
		static const ImVec4 STATIC_SHADOWED_COLOR = ImVec4(0.20f, 0.45f, 1.00f, 1.0f);
		static const ImVec4 STATIC_LIT_COLOR = ImVec4(0.25f, 1.00f, 0.45f, 1.0f);

		//One row of the legend, per cascade.
		struct CascadeRow {
			int index = 0;
			float near_split = 0.0f;
			float far_split = 0.0f;
			float radius = 0.0f;
			float texel_density = 0.0f;
		};

		static void Swatch(const ImVec4& color)
		{
			ImGui::ColorButton("##swatch", color,
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
				ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoBorder,
				ImVec2(14.0f, 14.0f));
		}

		static void DrawLegend(const std::string& light_name, int resolution,
			float shadow_distance, const std::vector<CascadeRow>& rows)
		{
			ImGui::SetNextWindowBgAlpha(0.78f);
			ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 24.0f),
				ImGuiCond_FirstUseEver, ImVec2(0.0f, 1.0f));
			if (ImGui::Begin("Shadow Cascades##shadow_debug", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
				ImGui::Text("%s  -  %d x %d per slice, %.0f units (dynamic casters)",
					light_name.c_str(), resolution, resolution, shadow_distance);
				if (ImGui::BeginTable("cascades", 5,
					ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
					ImGui::TableSetupColumn("##color");
					ImGui::TableSetupColumn("#");
					ImGui::TableSetupColumn("range");
					ImGui::TableSetupColumn("box");
					//The number the whole fit exists to maximise: shadow texels per world
					//unit in this cascade.
					ImGui::TableSetupColumn("texels/unit");
					ImGui::TableHeadersRow();
					for (const CascadeRow& r : rows) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						Swatch(CASCADE_COLOR[r.index % MAX_DRAWN_CASCADES]);
						ImGui::TableNextColumn();
						ImGui::Text("%d", r.index);
						ImGui::TableNextColumn();
						ImGui::Text("%.1f - %.1f", r.near_split, r.far_split);
						ImGui::TableNextColumn();
						ImGui::Text("%.1f", 2.0f * r.radius);
						ImGui::TableNextColumn();
						ImGui::Text("%.1f", r.texel_density);
					}
					//The band past the last cascade, which carries no shadow data and is
					//left lit. Worth a row of its own: seeing how much of the frame is
					//this colour is how you decide the shadow distance.
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					Swatch(OUTSIDE_COLOR);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("-");
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("beyond");
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("-");
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("unshadowed");
					ImGui::EndTable();
				}
			}
			ImGui::End();
		}

		//The static map's legend: what it covers and at what density. The extent is the
		//widest cascade's, since that is what it is fitted to.
		static void DrawStaticLegend(const std::string& light_name, int resolution,
			const DirectionalLight::CascadeInfo& info)
		{
			ImGui::SetNextWindowBgAlpha(0.78f);
			ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 24.0f),
				ImGuiCond_FirstUseEver, ImVec2(0.0f, 1.0f));
			if (ImGui::Begin("Static Shadow Map##shadow_debug_static", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
				if (info.radius <= 0.0f) {
					ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
						"%s: not rendered yet", light_name.c_str());
				}
				else {
					ImGui::Text("%s  -  %d x %d, box %.1f, %.1f texels/unit",
						light_name.c_str(), resolution, resolution,
						2.0f * info.radius, info.texel_density);
				}
				auto row = [](const ImVec4& color, const char* text) {
					ImGui::ColorButton("##swatch", color,
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
						ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoBorder,
						ImVec2(14.0f, 14.0f));
					ImGui::SameLine();
					ImGui::TextUnformatted(text);
				};
				row(STATIC_LIT_COLOR, "covered, no static caster overhead");
				row(STATIC_SHADOWED_COLOR, "shadowed by a static caster");
				//The one that matters: red is the map's edge made visible. It is
				//re-fitted when the camera carries the widest cascade off it, so a
				//permanent red region means the scene reaches past the shadow distance,
				//not that the refresh is lagging.
				row(STATIC_OUTSIDE_COLOR, "outside the static map - no static shadows");
			}
			ImGui::End();
		}

		void Draw(EditorState& state)
		{
			if (state.world == nullptr) {
				return;
			}
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				return;
			}

			//Pushed every frame, including Off: the flags live on the light, so leaving
			//one set after the view is changed would tint the scene with nothing on
			//screen to explain why.
			const bool cascades = state.shadow_debug_view == ShadowDebugView::Cascades;
			const bool static_map = state.shadow_debug_view == ShadowDebugView::StaticMap;
			const bool enabled = cascades || static_map;
			bool legend_done = false;
			for (const auto& [name, e] : c->GetEntites()) {
				if (!c->ContainsComponent<DirectionalLight>(e)) {
					continue;
				}
				DirectionalLight& light = c->GetComponent<DirectionalLight>(e);
				light.SetDebugCascades(cascades && light.CastShadow());
				light.SetDebugStaticShadow(static_map && light.CastShadow());
				if (!enabled || legend_done || !light.CastShadow()) {
					continue;
				}
				//First shadow-casting light wins the legend. More than one is legal but
				//rare, and their tints would be multiplied together anyway - at which
				//point no legend could describe the result.
				if (static_map) {
					DrawStaticLegend(name, light.GetStaticResolution(),
						light.GetStaticCascadeInfo());
					legend_done = true;
					continue;
				}
				std::vector<CascadeRow> rows;
				for (int i = 0; i < light.GetCascadeCount(); ++i) {
					const DirectionalLight::CascadeInfo& info = light.GetCascadeInfo(i);
					if (info.radius <= 0.0f) {
						continue;
					}
					rows.push_back(CascadeRow{ i, info.near_split, info.far_split,
						info.radius, info.texel_density });
				}
				if (!rows.empty()) {
					DrawLegend(name, light.GetCascadeResolution(),
						light.GetCascadeSettings().distance, rows);
					legend_done = true;
				}
			}
			if (enabled && !legend_done) {
				//Say why nothing is tinted rather than leaving the toggle looking broken:
				//no casting directional light, or one whose cascades have not been fitted
				//yet (the fit runs on the light system's first update).
				ImGui::GetBackgroundDrawList()->AddText(
					ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 130.0f, 60.0f),
					IM_COL32(255, 180, 60, 220),
					"Shadow cascades: no directional light casting shadows");
			}
		}

	}
}
