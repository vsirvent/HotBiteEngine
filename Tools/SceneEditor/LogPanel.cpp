#include "LogPanel.h"
#include "EditorLayout.h"

#include <Core/Log.h>

#include "imgui.h"

using namespace HotBite::Engine::Core;

namespace HotBiteEditor {
	namespace LogPanel {

		static const char* LevelLabel(LogLevel level) {
			switch (level) {
			case LogLevel::Trace:   return "TRACE";
			case LogLevel::Debug:   return "DEBUG";
			case LogLevel::Info:    return "INFO";
			case LogLevel::Warning: return "WARN";
			case LogLevel::Error:   return "ERROR";
			case LogLevel::Fatal:   return "FATAL";
			default:                return "?";
			}
		}

		//Whole-line severity colour, the common console convention: a wall of Info is
		//the uninteresting case, so what needs to stand out is the handful of Warning/
		//Error/Fatal lines in it, not a separately-coloured level tag someone has to
		//scan for.
		static ImVec4 LevelColor(LogLevel level) {
			switch (level) {
			case LogLevel::Trace:   return ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
			case LogLevel::Debug:   return ImVec4(0.65f, 0.68f, 0.78f, 1.0f);
			case LogLevel::Warning: return ImVec4(0.95f, 0.75f, 0.20f, 1.0f);
			case LogLevel::Error:   return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
			case LogLevel::Fatal:   return ImVec4(1.00f, 0.15f, 0.15f, 1.0f);
			case LogLevel::Info:
			default:                return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
			}
		}

		void Draw(EditorState& state)
		{
			(void)state;
			ImGui::Begin(EditorLayout::LOG_WINDOW);

			if (ImGui::Button("Clear")) {
				Log::ClearBuffer();
			}
			ImGui::SameLine();
			static bool auto_scroll = true;
			ImGui::Checkbox("Auto-scroll", &auto_scroll);
			ImGui::SameLine();

			//Changes what reaches the log (file and buffer) from here on; it does not
			//retroactively hide anything already captured, same as any logger's level.
			static const char* LEVEL_ITEMS[] = { "Trace", "Debug", "Info", "Warning", "Error", "Fatal", "Off" };
			int level_index = (int)Log::GetLevel();
			if (level_index < 0 || level_index >= IM_ARRAYSIZE(LEVEL_ITEMS)) {
				level_index = (int)LogLevel::Info;
			}
			ImGui::SetNextItemWidth(110.0f);
			if (ImGui::Combo("Level", &level_index, LEVEL_ITEMS, IM_ARRAYSIZE(LEVEL_ITEMS))) {
				Log::SetLevel((LogLevel)level_index);
			}

			ImGui::Separator();

			const std::vector<LogEntry> entries = Log::Snapshot();

			ImGui::BeginChild("##log_scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
			ImGuiListClipper clipper;
			clipper.Begin((int)entries.size());
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					const LogEntry& e = entries[(size_t)i];
					ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(e.level));
					ImGui::Text("%s [%-5s] %s:%d: %s",
						e.time.c_str(), LevelLabel(e.level), e.file.c_str(), e.line, e.text.c_str());
					ImGui::PopStyleColor();
				}
			}
			clipper.End();
			if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndChild();

			ImGui::End();
		}

	}
}
