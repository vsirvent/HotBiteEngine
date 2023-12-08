#pragma once

#include <Windows.h>
#include <Core\Json.h>

#pragma comment(lib,"HotBiteTool.lib")

using namespace nlohmann;

namespace HotBiteTool {
	namespace ToolUi {
		void CreateToolUi(HINSTANCE instance, HWND parent = NULL);
		void Run();
		void LoadUI(const json& ui);
		void DeleteToolUi();
	}
}
