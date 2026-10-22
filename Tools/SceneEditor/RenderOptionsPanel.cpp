#include "RenderOptionsPanel.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;

namespace RenderOptionsPanel {

	//Menu labels, in RenderSystem::eDebugBuffer order. The engine's own
	//DebugBufferName() gives the lower-case token the automation command takes;
	//these are the readable versions, and the two lists must stay aligned.
	const char* const DEBUG_BUFFER_LABELS[] = {
		"Off", "Scene colour", "Direct light", "Bloom", "Emission",
		"RT reflections", "RT refractions", "Indirect (GI)", "Volumetric light",
		"Dust", "Lens flare", "Depth", "World position", "World normal", "Motion vectors",
		"GI cache (world)", "GI cache confidence",
		//The ray source pair. "Mask" first because it is the one that answers a
		//question on its own; the four scalars are what it is made of.
		"Ray sources (mask)", "Ray dispersion", "Ray reflex", "Ray density",
		"Ray opacity",
		//The geometry the ray traced shadows are traced through, by triangle size and by cost.
		"Shadow BVH (triangle size)", "Shadow BVH (cost of a ray)"
	};
	static_assert(IM_ARRAYSIZE(DEBUG_BUFFER_LABELS) ==
		(int)RenderSystem::eDebugBuffer::COUNT,
		"DEBUG_BUFFER_LABELS must match RenderSystem::eDebugBuffer");

	//The indirect light's quality is its resolution: low traces it at a third of the screen's size on each side (what it always
	//was), medium at half, high at every pixel. Index 0..2 <-> divider 3, 2, 1.
	const char* const GI_QUALITY_NAMES[3] = { "low", "medium", "high" };
	uint32_t GiDividerOfQuality(int quality) { return quality <= 0 ? 3u : (quality == 1 ? 2u : 1u); }
	int GiQualityOfDivider(uint32_t divider) { return divider >= 3 ? 0 : (divider == 2 ? 1 : 2); }

	void Draw(RenderSystem* rs, Core::BaseDOFProcess* dof_effect)
	{
		static const char* quality_names[] = { "Off", "Low", "Mid", "High" };
		int quality = (int)rs->GetRayTracingQuality();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("Ray tracing", &quality, quality_names, IM_ARRAYSIZE(quality_names))) {
			rs->SetRayTracingQuality((RenderSystem::eRtQuality)quality);
		}

		static const char* gi_quality_labels[] = { "Low (1/3)", "Medium (1/2)", "High (full)" };
		int gi_quality = GiQualityOfDivider(rs->GetGIResolutionDivider());
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("GI quality", &gi_quality, gi_quality_labels, IM_ARRAYSIZE(gi_quality_labels))) {
			rs->SetGIResolutionDivider(GiDividerOfQuality(gi_quality));
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("The resolution of the indirect light's texture, on each side, against the screen's.\n"
				"Low: a third. Medium: a half. High: every pixel - sharper, and it costs more.");
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
		static const char* space_names[] = { "Screen", "World", "Screen + world" };
		//eRtSpace is a set of bits (1 screen, 2 world, 3 both): the combo is that minus one.
		int space = (int)rs->GetRayTracingSpace() - 1;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("RT solver", &space, space_names, IM_ARRAYSIZE(space_names))) {
			rs->SetRayTracingSpace((RenderSystem::eRtSpace)(space + 1));
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Where reflections are solved.\n"
				"Screen: marches the depth buffer - cheap, but only sees what is on screen.\n"
				"World: walks the BVH of the nearest objects - sees all of them, costs more.\n"
				"Screen + world: the screen first, the world for the rays it could not answer.\n"
				"Refractions are world only.");
		}
		int shadow_space = (int)rs->GetShadowRaySpace() - 1;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("RT shadow solver", &shadow_space, space_names, IM_ARRAYSIZE(space_names))) {
			rs->SetShadowRaySpace((RenderSystem::eRtSpace)(shadow_space + 1));
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Where the ray traced lights' shadows are solved.\n"
				"Screen: looks for a caster in the depth buffer - the only way a skinned mesh casts.\n"
				"World: walks the BVH of the casters (no skinned meshes), sees what is off screen.\n"
				"Screen + world: the screen first, the BVH when it finds nothing.");
		}
		float shadow_blend = rs->GetShadowRayBlend();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("RT shadow blend", &shadow_blend, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
			rs->SetShadowRayBlend(shadow_blend);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Weight of the newest frame in the running average of the ray traced shadows.\n"
				"Low: smooth, but a moving shadow leaves a ghost for about 1/blend frames.\n"
				"1: no averaging - no ghost, but the shadow edges are noisy.");
		}
		bool gtao = rs->GetAmbientOcclusion();
		if (ImGui::Checkbox("Ambient occlusion (GTAO)", &gtao)) {
			rs->SetAmbientOcclusion(gtao);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Ground truth ambient occlusion, traced against the Hi-Z pyramid of the depth map.\n"
				"One direction of each pixel a frame, averaged over the frames like the ray traced shadows.\n"
				"It takes the ambient light the surroundings hide out of the light map.");
		}
		ImGui::BeginDisabled(!gtao);
		float gtao_radius = rs->GetAmbientOcclusionRadius();
		float gtao_strength = rs->GetAmbientOcclusionStrength();
		float gtao_direct = rs->GetAmbientOcclusionDirect();
		bool gtao_changed = false;
		ImGui::SetNextItemWidth(120.0f);
		gtao_changed |= ImGui::SliderFloat("AO radius", &gtao_radius, 0.05f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How far the surroundings count, in world units.");
		}
		ImGui::SetNextItemWidth(120.0f);
		gtao_changed |= ImGui::SliderFloat("AO strength", &gtao_strength, 0.0f, 1.0f);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How much of the occlusion is taken out of the ambient light.");
		}
		ImGui::SetNextItemWidth(120.0f);
		gtao_changed |= ImGui::SliderFloat("AO on direct light", &gtao_direct, 0.0f, 1.0f);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("The share of the rest of the light (the sun's, the lamps') the occlusion dims as well.\n"
				"0: only the ambient light is hidden. 1: the whole light map is darkened.");
		}
		ImGui::EndDisabled();
		if (gtao_changed) {
			rs->SetAmbientOcclusionSettings(gtao_radius, gtao_strength, gtao_direct, rs->GetAmbientOcclusionSlices(), rs->GetAmbientOcclusionSteps());
		}
		float gi_confidence = rs->GetGIMaxConfidence();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("GI max confidence", &gi_confidence, 0.0f, 1.0f)) {
			rs->SetGIMaxConfidence(gi_confidence);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Ceiling on how much the indirect light trusts its history.\n"
				"1: a still camera converges fully - least noise, but it looks\n"
				"different from a moving one. 0: every pixel is treated as\n"
				"moving, so still and moving look alike, with more noise.");
		}

		ImGui::Separator();

		//Dynamic resolution: the scene is drawn smaller when the GPU would miss the refresh rate and DLSS makes the
		//native picture from it. No DLSS (not an RTX card, an old driver, no nvngx_dlss.dll), no option.
		const bool drs_available = rs->DynamicResolutionAvailable();
		bool drs = rs->GetDynamicResolution();
		ImGui::BeginDisabled(!drs_available);
		if (ImGui::Checkbox("Dynamic resolution (DLSS)", &drs)) {
			rs->SetDynamicResolution(drs);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			if (drs_available) {
				ImGui::SetTooltip("Draws the scene at a size that follows the GPU load, so the frame holds the target\n"
					"refresh rate, and upscales it with DLSS before the interface is drawn.");
			}
			else {
				ImGui::SetTooltip("Not available: %s.", rs->DynamicResolutionReason().c_str());
			}
		}
		if (drs_available) {
			//Shown, greyed out, while the option is off: the level's target rate and size range are stored with it
			//either way, and this is where they are set.
			ImGui::BeginDisabled(!drs);
			float hz = rs->GetDynamicResolutionTargetHz();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat("Target FPS", &hz, 24.0f, 360.0f, "%.0f")) {
				rs->SetDynamicResolutionTargetHz(hz);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("The frame rate the level holds: the render loop runs at it and the scene is drawn\n"
					"as big as that allows. Applies while dynamic resolution is on.");
			}
			float min_scale = rs->GetDynamicResolutionMinScale();
			float max_scale = rs->GetDynamicResolutionMaxScale();
			ImGui::SetNextItemWidth(120.0f);
			const bool min_changed = ImGui::SliderFloat("Min scale", &min_scale, 0.34f, 1.0f, "%.2f");
			ImGui::SetNextItemWidth(120.0f);
			const bool max_changed = ImGui::SliderFloat("Max scale", &max_scale, 0.34f, 1.0f, "%.2f");
			if (min_changed || max_changed) {
				rs->SetDynamicResolutionRange(min_scale, max_scale);
			}
			const RenderSystem::DynamicResolutionStats st = rs->GetDynamicResolutionStats();
			ImGui::TextDisabled("%dx%d (%.0f%%)  GPU %.1f ms of %.1f", st.width, st.height, st.scale * 100.0f, st.gpu_ms, st.budget_ms);
			ImGui::EndDisabled();
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
		ImGui::BeginDisabled(!motion_blur);
		float motion_blur_scale = rs->GetMotionBlurScale();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("Motion blur scale", &motion_blur_scale, 0.0f, 4.0f)) {
			rs->SetMotionBlurScale(motion_blur_scale);
		}
		ImGui::EndDisabled();
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
	}
}
