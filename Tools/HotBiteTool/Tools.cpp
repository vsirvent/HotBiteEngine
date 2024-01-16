#include "Tools.h"
#include <DirectXMath.h>
using namespace std;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::UI;

namespace HotBiteTool {
	namespace ToolUi {

		ToolUi::ToolUi(HINSTANCE hInstance, HWND parent, int32_t w, int32_t h, int32_t fps) : DXCore(hInstance, "ToolUi", w, h, true, true) {
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
			post = new DOPEffect(context, width, height, 0, 100);
			post->SetNext(gui);
			world.SetPostProcessPipeline(post);
			this->fps = fps;
			//Event listeners needs to be initialized once a coordinator is available as 
			//the coordinator manages the events
			EventListener::Init(c);
			world.Init();
			world.Run(fps);
			
			timer0 = Scheduler::Get(MAIN_THREAD)->RegisterTimer(1000000000 / 60, [this, rot = XMMatrixRotationAxis({0.0f, 1.0f, 0.0f}, 0.002f)](const Scheduler::TimerData&) {
				std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
				for (const auto& e : rotation_entities) {
					auto& t = world.GetCoordinator()->GetComponent<Components::Transform>(e);
					t.world_xmmatrix = XMMatrixMultiply(t.world_xmmatrix, rot);
					XMStoreFloat4x4(&t.world_matrix, XMMatrixTranspose(t.world_xmmatrix));
				}
				return true;
			});
		}

		void ToolUi::LoadUI(json ui) {
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			ECS::Coordinator* c = world.GetCoordinator();
			gui->LoadUI(c, ui);	
		}

		void ToolUi::SetMaterial(const std::string& entity, const std::string& root, const std::string& mat) {
			auto c = world.GetCoordinator();
			std::scoped_lock l(c->GetSystem<RenderSystem>()->mutex);
			ECS::Entity e = c->GetEntityByName(entity);
			auto& materials = world.GetMaterials();
			nlohmann::json j = nlohmann::json::parse(mat);
			std::string name = j["name"];
			MaterialData* mdata = materials.Get(name);
			if (mdata == nullptr) {
				materials.Insert(name, MaterialData{name});
				mdata = materials.Get(name);
			}
			mdata->Load(root, mat);
			if (e != ECS::INVALID_ENTITY_ID) {
				Components::Material& m = c->GetComponent<Components::Material>(e);
				m.multi_texture_count = 0;
				m.data = mdata;
				c->NotifySignatureChange(e);
			}
		}

		void ToolUi::SetMultiMaterial(const std::string& entity, const std::string& root, const std::string& mat) {
			auto c = world.GetCoordinator();
			std::scoped_lock l(c->GetSystem<RenderSystem>()->mutex);
			ECS::Entity e = c->GetEntityByName(entity);
			if (e != ECS::INVALID_ENTITY_ID) {
				Components::Material& m = c->GetComponent<Components::Material>(e);
				m.LoadMultitexture(mat, root, world.GetMaterials());
				c->NotifySignatureChange(e);
			}
		}

		void ToolUi::LoadWorld(const std::string& world_file)
		{
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			world.Stop();
			world.Release();
			world.PreLoad(this);
			delete gui;
			delete post;
			gui = new UI::GUI(context, width, height, world.GetCoordinator());
			post = new DOPEffect(context, width, height, 0, 100);
			post->SetNext(gui);
			world.SetPostProcessPipeline(post);
			world.Load(world_file);
			world.Init();
			world.Run(fps);
		}

		void ToolUi::RotateEntity(const std::string& name)
		{
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			auto entities = world.GetCoordinator()->GetEntitiesByName(name);
			for (const auto& e : entities) {
				rotation_entities.push_back(e);
			}
		}

		void ToolUi::SetVisible(const std::string& name, bool visible) {
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			auto entities = world.GetCoordinator()->GetEntitiesByName(name);
			for (const auto& e : entities) {
				auto& b = world.GetCoordinator()->GetComponent<Components::Base>(e);
				b.visible = visible;
			}
		}


		ToolUi::~ToolUi() {
			world.Stop();
			delete post;
			delete gui;
		}

		ECS::Coordinator* ToolUi::GetCoordinator() {
			return world.GetCoordinator();
		}
	}


}