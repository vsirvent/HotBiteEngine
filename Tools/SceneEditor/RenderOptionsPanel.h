#pragma once

// The Render menu of the Scene Editor as a panel of its own: every render option a level can set, as ImGui widgets that read and
// write the RenderSystem directly. The editor's Render menu and Survival's debug panel are both this one function, so a new option is
// added in one place.

#include <imgui.h>
#include <Core/PostProcess.h>
#include <Systems/RenderSystem.h>

namespace RenderOptionsPanel {
	// Menu labels, in RenderSystem::eDebugBuffer order (COUNT of them).
	extern const char* const DEBUG_BUFFER_LABELS[];

	// The indirect light's quality is the resolution of its texture: low traces it at a third of the screen's size on each side
	// (what it always was), medium at half, high at every pixel. Index 0..2 <-> divider 3, 2, 1.
	extern const char* const GI_QUALITY_NAMES[3];
	uint32_t GiDividerOfQuality(int quality);
	int GiQualityOfDivider(uint32_t divider);

	// The widgets, in the order of the Render menu. `dof_effect` may be null (the depth of field's focus and amplitude are then left out).
	void Draw(HotBite::Engine::Systems::RenderSystem* rs, HotBite::Engine::Core::BaseDOFProcess* dof_effect);
}
