#include "Tools.h"
#include "HotBiteTool.h"


namespace HotBiteTool {
	namespace ToolUi {
		ToolUi* tool_ui = nullptr;

		void CreateToolUi(HINSTANCE instance, HWND parent, int32_t w, int32_t h, int32_t fps) {
			if (tool_ui == nullptr) {
				tool_ui = new ToolUi(instance, parent, w, h, fps);
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

		void SetMaterial(const std::string& entity, const std::string& root, const std::string& mat) {
			if (tool_ui != nullptr) {
				tool_ui->SetMaterial(entity, root, mat);
			}
		}

		void SetMultiMaterial(const std::string& entity, const std::string& root, const std::string& mat) {
			if (tool_ui != nullptr) {
				tool_ui->SetMultiMaterial(entity, root, mat);
			}
		}

		void LoadWorld(const std::string& world_level) {
			if (tool_ui != nullptr) {
				tool_ui->LoadWorld(world_level);
			}
		}

		void RotateEntity(const std::string& name) {
			if (tool_ui != nullptr) {
				tool_ui->RotateEntity(name);
			}
		}

		void SetVisible(const std::string& name, bool visible) {
			if (tool_ui != nullptr) {
				tool_ui->SetVisible(name, visible);
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