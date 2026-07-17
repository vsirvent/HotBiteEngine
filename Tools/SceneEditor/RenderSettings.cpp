#include "RenderSettings.h"

#include "imgui.h"

#include <Core/PostProcess.h>
#include <Core/Json.h>
#include <Systems/RenderSystem.h>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;

namespace HotBiteEditor {
	namespace RenderSettings {

		static RenderSystem* GetRenderSystem(SceneEditorApp& app)
		{
			return app.GetState().world->GetSystem<RenderSystem>().get();
		}

		void ApplyHighDefaults(SceneEditorApp& app)
		{
			RenderSystem* rs = GetRenderSystem(app);
			rs->SetRayTracingQuality(RenderSystem::eRtQuality::HIGH);
			rs->SetRayTracing(true, true, true);
			rs->SetAA(true);
			rs->SetMotionBlur(true);
			rs->SetDOF(false);
			rs->SetLensFlare(true);
			rs->SetWireframe(false);
		}

		void DrawMenu(SceneEditorApp& app)
		{
			if (!ImGui::BeginMenu("Render", app.IsLevelLoaded())) {
				return;
			}
			RenderSystem* rs = GetRenderSystem(app);

			static const char* quality_names[] = { "Off", "Low", "Mid", "High" };
			int quality = (int)rs->GetRayTracingQuality();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::Combo("Ray tracing", &quality, quality_names, IM_ARRAYSIZE(quality_names))) {
				rs->SetRayTracingQuality((RenderSystem::eRtQuality)quality);
			}

			bool reflections, refractions, indirect;
			rs->GetRayTracing(reflections, refractions, indirect);
			bool rt_changed = false;
			rt_changed |= ImGui::Checkbox("RT reflections", &reflections);
			rt_changed |= ImGui::Checkbox("RT refractions", &refractions);
			rt_changed |= ImGui::Checkbox("RT indirect light", &indirect);
			if (rt_changed) {
				rs->SetRayTracing(reflections, refractions, indirect);
			}

			ImGui::Separator();

			bool aa = rs->GetAA();
			if (ImGui::Checkbox("Anti-aliasing", &aa)) {
				rs->SetAA(aa);
			}
			bool motion_blur = rs->GetMotionBlur();
			if (ImGui::Checkbox("Motion blur", &motion_blur)) {
				rs->SetMotionBlur(motion_blur);
			}
			bool lens_flare = rs->GetLensFlare();
			if (ImGui::Checkbox("Lens flare", &lens_flare)) {
				rs->SetLensFlare(lens_flare);
			}

			ImGui::Separator();

			bool dof = rs->GetDOF();
			if (ImGui::Checkbox("Depth of field", &dof)) {
				rs->SetDOF(dof);
			}
			Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
			if (dof_effect != nullptr) {
				float focus = dof_effect->GetFocus();
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderFloat("DOF focus", &focus, 1.0f, 200.0f)) {
					dof_effect->SetFocus(focus);
				}
				float amplitude = dof_effect->GetAmplitude();
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderFloat("DOF amplitude", &amplitude, 0.0f, 20.0f)) {
					dof_effect->SetAmplitude(amplitude);
				}
			}

			ImGui::Separator();

			bool wireframe = rs->GetWireframe();
			if (ImGui::Checkbox("Wireframe", &wireframe)) {
				rs->SetWireframe(wireframe);
			}

			ImGui::EndMenu();
		}

		static bool ParseBool(const std::string& value, bool& out)
		{
			if (value == "1" || value == "true" || value == "on") { out = true; return true; }
			if (value == "0" || value == "false" || value == "off") { out = false; return true; }
			return false;
		}

		bool Set(SceneEditorApp& app, const std::string& key, const std::string& value, std::string& error)
		{
			if (!app.IsLevelLoaded()) {
				error = "no level loaded";
				return false;
			}
			RenderSystem* rs = GetRenderSystem(app);

			if (key == "rt_quality") {
				RenderSystem::eRtQuality q;
				if (value == "off") { q = RenderSystem::eRtQuality::OFF; }
				else if (value == "low") { q = RenderSystem::eRtQuality::LOW; }
				else if (value == "mid") { q = RenderSystem::eRtQuality::MID; }
				else if (value == "high") { q = RenderSystem::eRtQuality::HIGH; }
				else { error = "rt_quality must be off|low|mid|high"; return false; }
				rs->SetRayTracingQuality(q);
				return true;
			}
			if (key == "rt_reflections" || key == "rt_refractions" || key == "rt_indirect") {
				bool enabled;
				if (!ParseBool(value, enabled)) { error = key + " must be 0|1"; return false; }
				bool reflections, refractions, indirect;
				rs->GetRayTracing(reflections, refractions, indirect);
				if (key == "rt_reflections") { reflections = enabled; }
				else if (key == "rt_refractions") { refractions = enabled; }
				else { indirect = enabled; }
				rs->SetRayTracing(reflections, refractions, indirect);
				return true;
			}
			if (key == "aa" || key == "motion_blur" || key == "dof" || key == "lens_flare" || key == "wireframe") {
				bool enabled;
				if (!ParseBool(value, enabled)) { error = key + " must be 0|1"; return false; }
				if (key == "aa") { rs->SetAA(enabled); }
				else if (key == "motion_blur") { rs->SetMotionBlur(enabled); }
				else if (key == "dof") { rs->SetDOF(enabled); }
				else if (key == "lens_flare") { rs->SetLensFlare(enabled); }
				else { rs->SetWireframe(enabled); }
				return true;
			}
			if (key == "dof_focus" || key == "dof_amplitude") {
				Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
				if (dof_effect == nullptr) { error = "no DOF effect installed"; return false; }
				float v;
				try { v = std::stof(value); }
				catch (...) { error = key + " must be a float"; return false; }
				if (key == "dof_focus") { dof_effect->SetFocus(v); }
				else { dof_effect->SetAmplitude(v); }
				return true;
			}
			error = "unknown render setting: " + key;
			return false;
		}

		std::string Dump(SceneEditorApp& app)
		{
			json j;
			if (app.IsLevelLoaded()) {
				RenderSystem* rs = GetRenderSystem(app);
				static const char* quality_names[] = { "off", "low", "mid", "high" };
				j["rt_quality"] = quality_names[(int)rs->GetRayTracingQuality()];
				bool reflections, refractions, indirect;
				rs->GetRayTracing(reflections, refractions, indirect);
				j["rt_reflections"] = reflections;
				j["rt_refractions"] = refractions;
				j["rt_indirect"] = indirect;
				j["aa"] = rs->GetAA();
				j["motion_blur"] = rs->GetMotionBlur();
				j["dof"] = rs->GetDOF();
				j["lens_flare"] = rs->GetLensFlare();
				j["wireframe"] = rs->GetWireframe();
				Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
				if (dof_effect != nullptr) {
					j["dof_focus"] = dof_effect->GetFocus();
					j["dof_amplitude"] = dof_effect->GetAmplitude();
				}
			}
			return j.dump();
		}

	}
}
