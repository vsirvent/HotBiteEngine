#include "Tools.h"
#include "HotBiteTool.h"


namespace HotBiteTool {
	namespace ToolUi {
		ToolUi* tool_ui = nullptr;

		void CreateToolUi(HINSTANCE instance, HWND parent) {
			tool_ui = new ToolUi(instance, parent);
		}

		void Run() {
			tool_ui->Run();
		}

		void LoadUI(const json& ui) {
			tool_ui->LoadUI(ui);
		}

		void DeleteToolUi() {
			delete tool_ui;
		}
	}
}