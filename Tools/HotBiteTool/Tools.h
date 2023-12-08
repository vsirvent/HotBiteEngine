#pragma once

#include <Core\PhysicsCommon.h>
#include <Core\DXCore.h>
#include <World.h>
#include <Windows.h>
#include <Systems\RTSCameraSystem.h>
#include <Systems\AudioSystem.h>
#include <GUI\GUI.h>
#include <GUI\Label.h>
#include <GUI\TextureWidget.h>
#include <GUI\ProgressBar.h>
#include <GUI\Button.h>

using namespace nlohmann;

namespace HotBiteTool {
	namespace ToolUi {

		class ToolUi : public DXCore, public ECS::EventListener {
		private:
			UI::GUI* gui = nullptr;

			World world;

		public:
			ToolUi(HINSTANCE hInstance, HWND parent);
			virtual ~ToolUi();

			void LoadUI(const json& ui);
			ECS::Coordinator* GetCoordinator();
		};
	}
}