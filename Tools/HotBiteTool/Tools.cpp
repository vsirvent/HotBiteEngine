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
			world.Run(5);
		}

		void ToolUi::LoadUI(json ui) {
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			ECS::Coordinator* c = world.GetCoordinator();
			gui->LoadUI(c, ui);			
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