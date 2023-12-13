#include "Tools.h"
#include "HotBiteTool.h"


namespace HotBiteTool {
	namespace ToolUi {
		ToolUi* tool_ui = nullptr;

		void CreateToolUi(HINSTANCE instance, HWND parent) {
			if (tool_ui == nullptr) {
				tool_ui = new ToolUi(instance, parent);
			}
		}

		void Run() {
			if (tool_ui != nullptr) {
				tool_ui->Run();
			}
		}

		void LoadUI(const json& ui) {
			if (tool_ui != nullptr) {
				tool_ui->LoadUI(ui);
			}
		}

		void DeleteToolUi() {
			if (tool_ui == nullptr) {
				delete tool_ui;
				tool_ui = nullptr;
			}
		}
	}
}