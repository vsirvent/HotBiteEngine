#include "Tools.h"

using namespace std;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::UI;

namespace HotBiteTool {
	namespace ToolUi {

		ToolUi::ToolUi(HINSTANCE hInstance, HWND parent) : DXCore(hInstance, "ToolUi", 1920, 1080, true, true) {
			//Initialize core DirectX
			InitWindow(parent);
			InitDirectX();
			//Initialize world
			world.PreLoad(this);
			//Get pointer to the world coordinator, this coordinator manages the 
			//world systems, entities, components and events.
			ECS::Coordinator* c = world.GetCoordinator();

			//we add to the chain a GUI post process stage to render UI
			gui = new UI::GUI(context, width, height, world.GetCoordinator());
			world.SetPostProcessPipeline(gui);

			//Event listeners needs to be initialized once a coordinator is available as 
			//the coordinator manages the events
			EventListener::Init(c);
			world.Init();
			world.Run(60);
		}

		void ToolUi::LoadUI(const json& ui) {
			world.GetCoordinator()->GetSystem<RenderSystem>()->mutex.lock();
			ECS::Coordinator* c = world.GetCoordinator();
			gui->Reset();
			auto& ui_node = ui["ui"];
			auto& widgets = ui_node["widgets"];
			for (const auto& widget : widgets) {
				if (widget["type"] == "widget") {
					//Create the UI in the GUI post process stage
					std::shared_ptr<UI::Widget> w = std::make_shared<UI::Widget>(c, widget);
					gui->AddWidget(w);
				}
			}
			world.GetCoordinator()->GetSystem<RenderSystem>()->mutex.unlock();
		}

		ToolUi::~ToolUi() {
			world.Stop();
			delete gui;
		}

		ECS::Coordinator* ToolUi::GetCoordinator() {
			return world.GetCoordinator();
		}
	}


}