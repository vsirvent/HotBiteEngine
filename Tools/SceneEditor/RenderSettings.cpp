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

		//Menu labels, in RenderSystem::eDebugBuffer order. The engine's own
		//DebugBufferName() gives the lower-case token the automation command takes;
		//these are the readable versions, and the two lists must stay aligned.
		static const char* DEBUG_BUFFER_LABELS[] = {
			"Off", "Scene colour", "Direct light", "Bloom", "Emission",
			"RT reflections", "RT refractions", "Indirect (GI)", "Volumetric light",
			"Dust", "Lens flare", "Depth", "World position", "World normal", "Motion vectors",
			"GI cache (world)", "GI cache confidence",
			//The ray source pair. "Mask" first because it is the one that answers a
			//question on its own; the four scalars are what it is made of.
			"Ray sources (mask)", "Ray dispersion", "Ray reflex", "Ray density",
			"Ray opacity"
		};
		static_assert(IM_ARRAYSIZE(DEBUG_BUFFER_LABELS) ==
			(int)RenderSystem::eDebugBuffer::COUNT,
			"DEBUG_BUFFER_LABELS must match RenderSystem::eDebugBuffer");

		void ApplyHighDefaults(SceneEditorApp& app)
		{
			RenderSystem* rs = GetRenderSystem(app);
			rs->SetRayTracingQuality(RenderSystem::eRtQuality::HIGH);
			rs->SetRayTracing(true, true, true);
			rs->SetAA(true);
			rs->SetMotionBlur(true);
			rs->SetDOF(false);
			rs->SetLensFlare(true);
			//Camera artifacts start neutral: the editor shows the render as it is,
			//and a look is something the user dials in deliberately.
			rs->SetLensEffects(true);
			rs->SetLensAberration(0.0f);
			rs->SetLensGrain(0.0f);
			rs->SetLensVignette(0.0f);
			rs->SetWireframe(false);
			//Debug views are a tool, never a level's state: a buffer view or a
			//bypassed denoiser carried into a freshly opened level would look like
			//the level rendering wrong.
			rs->SetRTDebug(0);
			rs->SetDebugGain(1.0f);
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
			bool autofocus = rs->GetDofAutofocus();
			if (ImGui::Checkbox("DOF autofocus", &autofocus)) {
				rs->SetDofAutofocus(autofocus);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Focal distance is measured on the GPU from the depth\n"
					"buffer: the distance to whatever is at the center of the view.");
			}
			Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
			if (dof_effect != nullptr) {
				float focus = dof_effect->GetFocus();
				ImGui::SetNextItemWidth(120.0f);
				//Under autofocus the live distance never reaches the CPU - it is
				//measured and consumed entirely on the GPU - so this slider holds
				//only the manual value, inert until autofocus is switched off.
				ImGui::BeginDisabled(autofocus);
				if (ImGui::SliderFloat("DOF focus", &focus, 1.0f, 200.0f) && !autofocus) {
					dof_effect->SetFocus(focus);
				}
				ImGui::EndDisabled();
				//The aperture is authored by hand in both modes; autofocus only ever
				//touches the focal distance.
				float amplitude = dof_effect->GetAmplitude();
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderFloat("DOF amplitude", &amplitude, 0.0f, 20.0f)) {
					dof_effect->SetAmplitude(amplitude);
				}
			}

			ImGui::Separator();

			//Physical camera artifacts. All three are 0..1 amounts; the master
			//checkbox zeroes them for an A/B without losing the settings.
			bool lens = rs->GetLensEffects();
			if (ImGui::Checkbox("Lens effects", &lens)) {
				rs->SetLensEffects(lens);
			}
			ImGui::BeginDisabled(!lens);
			float aberration = rs->GetLensAberration();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat("Chromatic aberration", &aberration, 0.0f, 1.0f)) {
				rs->SetLensAberration(aberration);
			}
			float grain = rs->GetLensGrain();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat("Film grain", &grain, 0.0f, 1.0f)) {
				rs->SetLensGrain(grain);
			}
			float vignette = rs->GetLensVignette();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat("Vignette", &vignette, 0.0f, 1.0f)) {
				rs->SetLensVignette(vignette);
			}
			ImGui::EndDisabled();

			ImGui::Separator();

			bool wireframe = rs->GetWireframe();
			if (ImGui::Checkbox("Wireframe", &wireframe)) {
				rs->SetWireframe(wireframe);
			}

			ImGui::Separator();

			//Buffer inspection. The combo drives ProcessMix, which writes the chosen
			//buffer to the screen in place of the mixed frame; the two bypasses turn
			//their denoiser into a copy so the buffer carries the raw traced signal.
			//Both are useful alone, and most useful together.
			int buffer = (int)rs->GetDebugBuffer();
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::Combo("Debug buffer", &buffer, DEBUG_BUFFER_LABELS,
				(int)RenderSystem::eDebugBuffer::COUNT)) {
				rs->SetDebugBuffer((RenderSystem::eDebugBuffer)buffer);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Shows one of the buffers the frame is mixed from.\n"
					"Anti-aliasing, motion blur, depth of field and the lens\n"
					"effects are suppressed while a buffer is selected.");
			}
			//Meaningful for the HDR colour buffers, and for the motion and ray scalar
			//ramps, whose ranges have no natural display scale. depth/position/normal
			//and the two cache views are mapped, not exposed, so it does nothing there.
			ImGui::BeginDisabled(!rs->IsDebugBufferActive());
			float gain = rs->GetDebugGain();
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::SliderFloat("Debug gain", &gain, 0.0f, 32.0f, "%.2fx",
				ImGuiSliderFlags_Logarithmic)) {
				rs->SetDebugGain(gain);
			}
			ImGui::EndDisabled();

			bool no_gi_denoise = rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_GI_DENOISE);
			if (ImGui::Checkbox("Bypass GI denoise", &no_gi_denoise)) {
				rs->SetDebugFlag(RenderSystem::RT_DEBUG_NO_GI_DENOISE, no_gi_denoise);
			}
			bool no_rt_denoise = rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_RT_DENOISE);
			if (ImGui::Checkbox("Bypass RT denoise", &no_rt_denoise)) {
				rs->SetDebugFlag(RenderSystem::RT_DEBUG_NO_RT_DENOISE, no_rt_denoise);
			}

			ImGui::EndMenu();
		}

		//These MUST match the RAY_DEBUG_* colours in
		//Shaders/Common/RenderDebug.hlsli. The shader paints the classes; this only
		//names them, and a legend that disagrees with the screen is worse than none.
		static const ImVec4 RAY_RT_OFF_COLOR = ImVec4(0.10f, 0.12f, 0.55f, 1.0f);
		static const ImVec4 RAY_NO_REFLEX_COLOR = ImVec4(0.25f, 0.45f, 0.85f, 1.0f);
		static const ImVec4 RAY_GI_ONLY_COLOR = ImVec4(0.95f, 0.60f, 0.10f, 1.0f);
		static const ImVec4 RAY_REFLECT_COLOR = ImVec4(0.15f, 0.85f, 0.25f, 1.0f);
		static const ImVec4 RAY_REFRACT_COLOR = ImVec4(0.15f, 0.85f, 0.85f, 1.0f);
		static const ImVec4 RAY_NO_SOURCE_COLOR = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);

		static void SwatchRow(const ImVec4& color, const char* text)
		{
			ImGui::ColorButton("##swatch", color,
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
				ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoBorder,
				ImVec2(14.0f, 14.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted(text);
		}

		//What the ray source mask is showing: which pixels the tracers accept, and for
		//the ones they reject, which test rejected them. Ordered from "traces the most"
		//down to "traces nothing", because that is the direction you read it in when
		//asking why a surface has no reflection.
		static void DrawRayMaskLegend()
		{
			ImGui::TextUnformatted("rt_ray_sources0/1, as the tracers read them:");
			SwatchRow(RAY_REFRACT_COLOR, "reflection + refraction + GI");
			SwatchRow(RAY_REFLECT_COLOR, "reflection + GI");
			SwatchRow(RAY_GI_ONLY_COLOR, "GI only - dispersion >= 1, too rough to reflect");
			SwatchRow(RAY_NO_REFLEX_COLOR, "no rays - the material's rt_reflex is 0");
			SwatchRow(RAY_RT_OFF_COLOR, "no rays - ray tracing off on this material");
			SwatchRow(RAY_NO_SOURCE_COLOR, "black: no ray source written here");
		}

		//The four packed scalars share one ramp, and unlike the other mapped views they
		//honour the gain - their ranges differ per scalar, so there is no single natural
		//display range to bake in.
		static void DrawRayScalarLegend(float gain, const char* line1, const char* line2)
		{
			ImGui::Text("x %.2f gain: blue 0, green 0.5, red 1, magenta above 1.", gain);
			ImGui::TextUnformatted("Black where no ray source was written.");
			ImGui::TextUnformatted(line1);
			ImGui::TextUnformatted(line2);
		}

		void DrawOverlay(SceneEditorApp& app)
		{
			if (!app.IsLevelLoaded()) {
				return;
			}
			RenderSystem* rs = GetRenderSystem(app);
			const bool gi_bypassed = rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_GI_DENOISE);
			const bool rt_bypassed = rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_RT_DENOISE);
			if (!rs->IsDebugBufferActive()) {
				//A bypassed denoiser with no buffer view still changes the frame, and
				//it is the kind of switch that gets left on and then blamed on the
				//renderer. Keep saying so until it is turned off.
				if (gi_bypassed || rt_bypassed) {
					ImGui::GetBackgroundDrawList()->AddText(
						ImVec2(12.0f, 26.0f), IM_COL32(255, 180, 60, 220),
						gi_bypassed && rt_bypassed ? "GI + RT denoise bypassed"
						: (gi_bypassed ? "GI denoise bypassed" : "RT denoise bypassed"));
				}
				return;
			}

			ImGui::SetNextWindowBgAlpha(0.78f);
			//Top centre: the docked panels take the left and right edges and the
			//shadow legends take the bottom left, so this is the one part of the
			//frame nothing else claims.
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 32.0f),
				ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
			if (ImGui::Begin("Debug Buffer##render_debug", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
				const int buffer = (int)rs->GetDebugBuffer();
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s",
					DEBUG_BUFFER_LABELS[buffer]);
				switch (rs->GetDebugBuffer()) {
				case RenderSystem::eDebugBuffer::DEPTH:
					ImGui::TextUnformatted("world distance, 1-exp(-d/100); black = nothing hit");
					break;
				case RenderSystem::eDebugBuffer::POSITION:
					ImGui::TextUnformatted("world position, repeating every 10 units per axis");
					break;
				case RenderSystem::eDebugBuffer::NORMAL:
					ImGui::TextUnformatted("world normal, remapped from -1..1");
					break;
				case RenderSystem::eDebugBuffer::MOTION:
					//Motion is the one mapped view that still honours the gain, so say so.
					ImGui::Text("screen motion x %.2f gain; grey = not moving,",
						rs->GetDebugGain());
					ImGui::TextUnformatted("red/green = +x/+y, blue = nothing drawn");
					break;
				case RenderSystem::eDebugBuffer::RAY_SOURCES:
					DrawRayMaskLegend();
					break;
				case RenderSystem::eDebugBuffer::RAY_DISPERSION:
					DrawRayScalarLegend(rs->GetDebugGain(),
						"1 - specular intensity, and 2.0 where the material has ray",
						"tracing off. At or above 1 no reflection ray is traced.");
					break;
				case RenderSystem::eDebugBuffer::RAY_REFLEX:
					DrawRayScalarLegend(rs->GetDebugGain(),
						"MaterialProps::rt_reflex, the share of the reflection kept.",
						"0 traces nothing at all here - not even indirect light.");
					break;
				case RenderSystem::eDebugBuffer::RAY_DENSITY:
					DrawRayScalarLegend(rs->GetDebugGain(),
						"refraction index, 1.0 and up - try 0.5x gain to fit the ramp.",
						"Only read where opacity is below 1.");
					break;
				case RenderSystem::eDebugBuffer::RAY_OPACITY:
					DrawRayScalarLegend(rs->GetDebugGain(),
						"surface opacity. Below 1 is what spawns a refraction ray,",
						"so anything at the top of the ramp refracts nothing.");
					break;
				default:
					ImGui::Text("raw buffer x %.2f gain, clamped to 0..1", rs->GetDebugGain());
					break;
				}
				if (gi_bypassed || rt_bypassed) {
					ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "%s",
						gi_bypassed && rt_bypassed ? "GI + RT denoise bypassed"
						: (gi_bypassed ? "GI denoise bypassed" : "RT denoise bypassed"));
				}
				ImGui::Separator();
				//Say it plainly: these are off because the view is on, not because
				//the settings were changed.
				ImGui::TextDisabled("AA / motion blur / DOF / lens suppressed");
			}
			ImGui::End();
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
			if (key == "aa" || key == "motion_blur" || key == "dof" || key == "lens_flare" ||
				key == "lens" || key == "wireframe") {
				bool enabled;
				if (!ParseBool(value, enabled)) { error = key + " must be 0|1"; return false; }
				if (key == "aa") { rs->SetAA(enabled); }
				else if (key == "motion_blur") { rs->SetMotionBlur(enabled); }
				else if (key == "dof") { rs->SetDOF(enabled); }
				else if (key == "lens_flare") { rs->SetLensFlare(enabled); }
				else if (key == "lens") { rs->SetLensEffects(enabled); }
				else { rs->SetWireframe(enabled); }
				return true;
			}
			if (key == "lens_aberration" || key == "lens_grain" || key == "lens_vignette") {
				float v;
				try { v = std::stof(value); }
				catch (...) { error = key + " must be a float"; return false; }
				if (v < 0.0f || v > 1.0f) { error = key + " must be within 0..1"; return false; }
				if (key == "lens_aberration") { rs->SetLensAberration(v); }
				else if (key == "lens_grain") { rs->SetLensGrain(v); }
				else { rs->SetLensVignette(v); }
				return true;
			}
			if (key == "dof_autofocus") {
				bool enabled;
				if (!ParseBool(value, enabled)) { error = key + " must be 0|1"; return false; }
				rs->SetDofAutofocus(enabled);
				return true;
			}
			if (key == "dof_focus" || key == "dof_amplitude") {
				Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
				if (dof_effect == nullptr) { error = "no DOF effect installed"; return false; }
				float v;
				try { v = std::stof(value); }
				catch (...) { error = key + " must be a float"; return false; }
				if (key == "dof_focus") {
					//Setting the distance by hand is an implicit switch to manual
					//mode; autofocus would otherwise override it every frame. The
					//aperture is independent and leaves the mode alone.
					rs->SetDofAutofocus(false);
					dof_effect->SetFocus(v);
				}
				else { dof_effect->SetAmplitude(v); }
				return true;
			}
			if (key == "debug_buffer") {
				//Matched against the engine's own token list so the command and the
				//shader can never drift apart on what "indirect" means.
				for (int i = 0; i < (int)RenderSystem::eDebugBuffer::COUNT; ++i) {
					RenderSystem::eDebugBuffer b = (RenderSystem::eDebugBuffer)i;
					if (value == RenderSystem::DebugBufferName(b)) {
						rs->SetDebugBuffer(b);
						return true;
					}
				}
				error = "debug_buffer must be one of:";
				for (int i = 0; i < (int)RenderSystem::eDebugBuffer::COUNT; ++i) {
					error += " ";
					error += RenderSystem::DebugBufferName((RenderSystem::eDebugBuffer)i);
				}
				return false;
			}
			if (key == "gi_denoise" || key == "rt_denoise") {
				//Phrased as the feature, not the bypass: "gi_denoise 0" reads better
				//than "no_gi_denoise 1", so the stored flag is the inverse of the value.
				bool enabled;
				if (!ParseBool(value, enabled)) { error = key + " must be 0|1"; return false; }
				rs->SetDebugFlag(key == "gi_denoise"
					? RenderSystem::RT_DEBUG_NO_GI_DENOISE
					: RenderSystem::RT_DEBUG_NO_RT_DENOISE, !enabled);
				return true;
			}
			if (key == "debug_gain") {
				float v;
				try { v = std::stof(value); }
				catch (...) { error = key + " must be a float"; return false; }
				if (v < 0.0f) { error = "debug_gain must be >= 0"; return false; }
				rs->SetDebugGain(v);
				return true;
			}
			error = "unknown render setting: " + key;
			return false;
		}

		std::string Dump(SceneEditorApp& app)
		{
			json j = ToJson(app);
			if (app.IsLevelLoaded()) {
				RenderSystem* rs = GetRenderSystem(app);
				j["debug_buffer"] = RenderSystem::DebugBufferName(rs->GetDebugBuffer());
				j["debug_gain"] = rs->GetDebugGain();
				j["gi_denoise"] = !rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_GI_DENOISE);
				j["rt_denoise"] = !rs->GetDebugFlag(RenderSystem::RT_DEBUG_NO_RT_DENOISE);
			}
			return j.dump();
		}

		nlohmann::json ToJson(SceneEditorApp& app)
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
				j["dof_autofocus"] = rs->GetDofAutofocus();
				j["lens_flare"] = rs->GetLensFlare();
				j["lens"] = rs->GetLensEffects();
				j["lens_aberration"] = rs->GetLensAberration();
				j["lens_grain"] = rs->GetLensGrain();
				j["lens_vignette"] = rs->GetLensVignette();
				j["wireframe"] = rs->GetWireframe();
				Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
				if (dof_effect != nullptr) {
					j["dof_focus"] = dof_effect->GetFocus();
					j["dof_amplitude"] = dof_effect->GetAmplitude();
				}
			}
			return j;
		}

		void ApplyFromJson(SceneEditorApp& app, const nlohmann::json& j)
		{
			//No IsLevelLoaded() guard, deliberately: like ApplyHighDefaults, this runs
			//from OpenLevel in the window after the post-process pipeline exists but
			//before level_loaded is flipped to true, so that check would always fail.
			if (!j.is_object()) {
				return;
			}
			RenderSystem* rs = GetRenderSystem(app);

			if (j.contains("rt_quality") && j["rt_quality"].is_string()) {
				const std::string q = j["rt_quality"];
				if (q == "off") { rs->SetRayTracingQuality(RenderSystem::eRtQuality::OFF); }
				else if (q == "low") { rs->SetRayTracingQuality(RenderSystem::eRtQuality::LOW); }
				else if (q == "mid") { rs->SetRayTracingQuality(RenderSystem::eRtQuality::MID); }
				else if (q == "high") { rs->SetRayTracingQuality(RenderSystem::eRtQuality::HIGH); }
			}
			//The three RT flags always go through SetRayTracing together (it takes all
			//three at once), so read back what ApplyHighDefaults already set for
			//whichever of the three is missing from an older/hand-edited file.
			if (j.contains("rt_reflections") || j.contains("rt_refractions") || j.contains("rt_indirect")) {
				bool reflections, refractions, indirect;
				rs->GetRayTracing(reflections, refractions, indirect);
				reflections = j.value("rt_reflections", reflections);
				refractions = j.value("rt_refractions", refractions);
				indirect = j.value("rt_indirect", indirect);
				rs->SetRayTracing(reflections, refractions, indirect);
			}
			if (j.contains("aa")) { rs->SetAA(j.value("aa", rs->GetAA())); }
			if (j.contains("motion_blur")) { rs->SetMotionBlur(j.value("motion_blur", rs->GetMotionBlur())); }
			if (j.contains("dof")) { rs->SetDOF(j.value("dof", rs->GetDOF())); }
			if (j.contains("dof_autofocus")) { rs->SetDofAutofocus(j.value("dof_autofocus", rs->GetDofAutofocus())); }
			if (j.contains("lens_flare")) { rs->SetLensFlare(j.value("lens_flare", rs->GetLensFlare())); }
			if (j.contains("lens")) { rs->SetLensEffects(j.value("lens", rs->GetLensEffects())); }
			if (j.contains("lens_aberration")) { rs->SetLensAberration(j.value("lens_aberration", rs->GetLensAberration())); }
			if (j.contains("lens_grain")) { rs->SetLensGrain(j.value("lens_grain", rs->GetLensGrain())); }
			if (j.contains("lens_vignette")) { rs->SetLensVignette(j.value("lens_vignette", rs->GetLensVignette())); }
			if (j.contains("wireframe")) { rs->SetWireframe(j.value("wireframe", rs->GetWireframe())); }

			Core::BaseDOFProcess* dof_effect = app.GetDofEffect();
			if (dof_effect != nullptr) {
				//A stored focus is a manual value; restoring it must not fight
				//autofocus, which SetDofAutofocus(true) above may just have turned
				//back on and which overrides the focus every frame while active.
				if (j.contains("dof_focus")) {
					dof_effect->SetFocus(j.value("dof_focus", dof_effect->GetFocus()));
				}
				if (j.contains("dof_amplitude")) {
					dof_effect->SetAmplitude(j.value("dof_amplitude", dof_effect->GetAmplitude()));
				}
			}
		}

	}
}
