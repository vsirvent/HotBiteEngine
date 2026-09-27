#include "AgentPanel.h"
#include "ClaudeAgent.h"
#include "EditorHistory.h"
#include "EditorLayout.h"

#include "imgui.h"

#include <cstring>

namespace HotBiteEditor {
	namespace AgentPanel {

		namespace {
			//What the message box holds. Enter sends, Ctrl+Enter is a new line.
			char input[16384] = {};
			bool refocus_input = false;
			uint64_t seen_revision = 0;

			//Setup is checked when the panel first shows and on Retry, not every frame:
			//it walks the file system.
			bool setup_checked = false;
			ClaudeAgent::Setup setup;

			const ImVec4 COLOR_USER(0.55f, 0.75f, 1.00f, 1.0f);
			const ImVec4 COLOR_DIM(0.60f, 0.60f, 0.63f, 1.0f);
			const ImVec4 COLOR_TOOL(0.70f, 0.70f, 0.75f, 1.0f);
			const ImVec4 COLOR_RUNNING(0.95f, 0.80f, 0.30f, 1.0f);
			const ImVec4 COLOR_ERROR(0.95f, 0.40f, 0.40f, 1.0f);
			const ImVec4 COLOR_OK(0.45f, 0.85f, 0.50f, 1.0f);

			//What the panel offers: the value passed to --model, and what the person reads.
			//Aliases, so each always means that family's newest model.
			struct ModelChoice {
				const char* value;
				const char* label;
				const char* hint;
			};
			const ModelChoice MODELS[] = {
				{ "", "Default", "Whatever Claude Code defaults to (usually the most capable, and most expensive, model)" },
				{ "opus", "Opus", "Most capable, highest cost per message" },
				{ "sonnet", "Sonnet", "Balanced: good at level edits, several times cheaper than Opus" },
				{ "haiku", "Haiku", "Fastest and cheapest: simple edits and questions" },
			};

			ImVec4 StatusColor(ClaudeAgent::Status s)
			{
				switch (s) {
				case ClaudeAgent::Status::Busy:
				case ClaudeAgent::Status::Stopping: return COLOR_RUNNING;
				case ClaudeAgent::Status::Failed: return COLOR_ERROR;
				case ClaudeAgent::Status::Ready: return COLOR_OK;
				default: return COLOR_DIM;
				}
			}

			//A right-click Copy on the item just drawn: plain ImGui text cannot be
			//selected, and copying what the agent said is the commonest thing to want.
			void CopyMenu(const std::string& text)
			{
				if (ImGui::BeginPopupContextItem("copy")) {
					if (ImGui::MenuItem("Copy")) {
						ImGui::SetClipboardText(text.c_str());
					}
					ImGui::EndPopup();
				}
			}

			std::string Clip(const std::string& s, size_t n)
			{
				return s.size() > n ? s.substr(0, n) + "\n... (" + std::to_string(s.size() - n) + " more characters)" : s;
			}

			void DrawToolbar(EditorState& state)
			{
				const ClaudeAgent::Status status = ClaudeAgent::GetStatus();
				const bool busy = status == ClaudeAgent::Status::Busy || status == ClaudeAgent::Status::Stopping;

				ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(status));
				ImGui::Text("[%s]", ClaudeAgent::StatusName(status));
				ImGui::PopStyleColor();
				ImGui::SameLine();
				const std::string active = ClaudeAgent::ActiveModel();
				ImGui::TextDisabled("%s  $%.2f", active.empty() ? "" : active.c_str(), ClaudeAgent::TotalCostUsd());

				//The model picker, remembered across sessions. A model that is not one of
				//the aliases (set by automation) shows as itself.
				const std::string current = ClaudeAgent::Model();
				const ModelChoice* selected = nullptr;
				for (const ModelChoice& m : MODELS) {
					if (current == m.value) selected = &m;
				}
				ImGui::TextUnformatted("Model");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(110.0f);
				if (ImGui::BeginCombo("##model", selected ? selected->label : current.c_str())) {
					for (const ModelChoice& m : MODELS) {
						if (ImGui::Selectable(m.label, selected == &m)) {
							ClaudeAgent::SetModel(m.value, true);
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", m.hint);
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Used from the next message on, and remembered for later sessions.\n"
						"Switching mid-conversation restarts Claude and re-reads the conversation once.");
				}
				ImGui::SameLine();
				if (ImGui::Button("New chat")) {
					ClaudeAgent::NewConversation();
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(status != ClaudeAgent::Status::Busy);
				if (ImGui::Button("Stop")) {
					ClaudeAgent::Stop();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				//Only offered when the step on top of the history is an agent turn, so the
				//button never undoes something the person did by hand.
				const std::string top = EditorHistory::TopDescription();
				const bool can_undo_turn = !busy && top.rfind("Claude: ", 0) == 0;
				ImGui::BeginDisabled(!can_undo_turn);
				if (ImGui::Button("Undo turn")) {
					std::string error;
					if (!EditorHistory::Undo(state, error)) {
						state.status_message = "Undo failed: " + error;
					}
				}
				ImGui::EndDisabled();
				if (can_undo_turn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("%s", top.c_str());
				}
			}

			void DrawItem(const ClaudeAgent::Item& item, int index)
			{
				using Kind = ClaudeAgent::Item::Kind;
				ImGui::PushID(index);
				switch (item.kind) {
				case Kind::User:
					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_USER);
					ImGui::TextWrapped("> %s", item.text.c_str());
					ImGui::PopStyleColor();
					CopyMenu(item.text);
					break;
				case Kind::Assistant:
					ImGui::TextWrapped("%s", item.text.c_str());
					CopyMenu(item.text);
					break;
				case Kind::Tool: {
					const ImVec4 color = !item.tool_done ? COLOR_RUNNING : item.tool_error ? COLOR_ERROR : COLOR_TOOL;
					ImGui::PushStyleColor(ImGuiCol_Text, color);
					const bool open = ImGui::TreeNodeEx("tool", ImGuiTreeNodeFlags_SpanAvailWidth, "%s %s%s",
						item.tool_done ? (item.tool_error ? "x" : "-") : "~", item.tool_name.c_str(),
						item.tool_images > 0 ? "  [image]" : "");
					ImGui::PopStyleColor();
					if (open) {
						ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
						ImGui::TextWrapped("%s", Clip(item.text, 2000).c_str());
						CopyMenu(item.text);
						if (item.tool_done) {
							ImGui::Separator();
							ImGui::TextWrapped("%s", Clip(item.tool_result, 6000).c_str());
							ImGui::PushID("result");
							CopyMenu(item.tool_result);
							ImGui::PopID();
						}
						ImGui::PopStyleColor();
						ImGui::TreePop();
					}
					break;
				}
				case Kind::Info:
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
					ImGui::TextWrapped("%s", item.text.c_str());
					ImGui::PopStyleColor();
					break;
				case Kind::Error:
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
					ImGui::TextWrapped("%s", item.text.c_str());
					ImGui::PopStyleColor();
					CopyMenu(item.text);
					break;
				case Kind::Result:
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
					ImGui::TextUnformatted(item.text.c_str());
					ImGui::PopStyleColor();
					break;
				}
				ImGui::PopID();
			}

			void DrawTranscript(float height)
			{
				ImGui::BeginChild("##transcript", ImVec2(0.0f, height), ImGuiChildFlags_Border);
				//Follow new output only while the view is already at the bottom, so
				//scrolling up to read something is not yanked away by the next event.
				const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;

				const auto& transcript = ClaudeAgent::Transcript();
				if (transcript.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
					ImGui::TextWrapped("Ask Claude about this level, or to change it: \"what is in this scene?\", "
						"\"line the three boxes up along X\", \"add a red point light above the troll\".\n\n"
						"Everything Claude does for one message is one undo step.");
					ImGui::PopStyleColor();
				}
				for (int i = 0; i < (int)transcript.size(); ++i) {
					DrawItem(transcript[(size_t)i], i);
				}
				const std::string live = ClaudeAgent::LiveText();
				if (!live.empty()) {
					ImGui::TextWrapped("%s", live.c_str());
				}
				if (ClaudeAgent::GetStatus() == ClaudeAgent::Status::Busy) {
					ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RUNNING);
					const int dots = (int)(ImGui::GetTime() * 3.0) % 4;
					ImGui::Text("working%.*s", dots, "...");
					ImGui::PopStyleColor();
				}

				if (ClaudeAgent::Revision() != seen_revision) {
					seen_revision = ClaudeAgent::Revision();
					if (at_bottom) {
						ImGui::SetScrollHereY(1.0f);
					}
				}
				ImGui::EndChild();
			}

			void SendInput(EditorState& state)
			{
				std::string error;
				if (ClaudeAgent::Send(state, input, error)) {
					input[0] = '\0';
				}
				else if (error != "empty message") {
					state.status_message = "Claude: " + error;
				}
				refocus_input = true;
			}

			void DrawInput(EditorState& state)
			{
				const bool busy = ClaudeAgent::GetStatus() == ClaudeAgent::Status::Busy ||
					ClaudeAgent::GetStatus() == ClaudeAgent::Status::Stopping;
				const float button_width = 60.0f;
				const float height = ImGui::GetTextLineHeight() * 4.0f + ImGui::GetStyle().FramePadding.y * 2.0f;
				if (refocus_input) {
					ImGui::SetKeyboardFocusHere();
					refocus_input = false;
				}
				ImGui::SetNextItemWidth(-(button_width + ImGui::GetStyle().ItemSpacing.x));
				const bool entered = ImGui::InputTextMultiline("##message", input, sizeof(input),
					ImVec2(-(button_width + ImGui::GetStyle().ItemSpacing.x), height),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
				ImGui::SameLine();
				ImGui::BeginDisabled(busy);
				const bool clicked = ImGui::Button("Send", ImVec2(button_width, height));
				ImGui::EndDisabled();
				if ((entered || clicked) && !busy) {
					SendInput(state);
				}
			}
		}

		void Draw(EditorState& state)
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			const ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.50f, vp->WorkPos.y + vp->WorkSize.y * 0.08f), cond);
			ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.26f, vp->WorkSize.y * 0.70f), cond);
			if (!ImGui::Begin(EditorLayout::AGENT_WINDOW, &state.show_agent_panel)) {
				ImGui::End();
				return;
			}

			if (!setup_checked) {
				setup = ClaudeAgent::Locate(state);
				setup_checked = true;
			}
			if (!setup.problem.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
				ImGui::TextWrapped("%s", setup.problem.c_str());
				ImGui::PopStyleColor();
				if (ImGui::Button("Retry")) {
					setup_checked = false;
				}
				ImGui::End();
				return;
			}

			DrawToolbar(state);
			ImGui::Separator();
			const float input_height = ImGui::GetTextLineHeight() * 4.0f + ImGui::GetStyle().FramePadding.y * 2.0f +
				ImGui::GetStyle().ItemSpacing.y * 2.0f;
			DrawTranscript(ImGui::GetContentRegionAvail().y - input_height);
			DrawInput(state);
			ImGui::End();
		}
	}
}
