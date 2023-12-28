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
			Scheduler::TimerId timer0;
			std::vector<ECS::Entity> rotation_entities;
			World world;
			int32_t fps = 30;

		public:
			ToolUi(HINSTANCE hInstance, HWND parent, int32_t w, int32_t h, int32_t fps);
			virtual ~ToolUi();

			void LoadUI(json ui);
			void SetMaterial(const std::string& entity, const std::string& mat);
			void LoadWorld(const std::string& world);
			void RotateEntity(const std::string& name);
			ECS::Coordinator* GetCoordinator();
		};
	}
}