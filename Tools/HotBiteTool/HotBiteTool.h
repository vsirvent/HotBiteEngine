#pragma once

#include <Windows.h>
#include <Core\Json.h>

#pragma comment(lib,"HotBiteTool.lib")

using namespace nlohmann;

namespace HotBiteTool {
	namespace ToolUi {
		void CreateToolUi(HINSTANCE instance, HWND parent = NULL, int32_t w = 0, int32_t h = 0, int32_t fps = 30);
		void Run();
		void LoadUI(const json& ui);
		void ReloadShaders();
		void SetMaterial(const std::string& entity, const std::string& root, const std::string& mat);
		void SetMultiMaterial(const std::string& entity, const std::string& root, const std::string& mat);
		void LoadWorld(const std::string& world_level);
		void RotateEntity(const std::string& name);
		void SetVisible(const std::string& name, bool visible);
		void DeleteToolUi();
	}
}
