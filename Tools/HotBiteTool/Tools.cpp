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
			post = new MainEffect(context, width, height);
			post->SetNext(gui);
			world.SetPostProcessPipeline(post);
			this->fps = fps;
			//Event listeners needs to be initialized once a coordinator is available as 
			//the coordinator manages the events
			EventListener::Init(c);
			world.Init();
			world.Run(fps);
			timer0 = Scheduler::Get(MAIN_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData&) {
				std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
				static float rot_val = 0.0f;
				
				auto rot = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, rot_val);
				vector3d move_q = XMQuaternionRotationMatrix(rot);
				rot_val += 0.1f * sin((float)timeGetTime() / 1000.0f);
				if (rot_val > XM_PI) rot_val = -XM_PI;
				for (const auto& e : rotation_entities) {
					auto& t = world.GetCoordinator()->GetComponent<Components::Transform>(e);
					vector3d rot_q = XMVectorSet(t.initial_rotation.x, t.initial_rotation.y,
					t.initial_rotation.z, t.initial_rotation.w);
					auto r = XMQuaternionMultiply(rot_q, move_q);
					XMStoreFloat4(&t.rotation, r);
					t.dirty = true;
				}
				return true;
			});
		}

		void ToolUi::ReloadShaders() {
			ShaderFactory::Get()->Reload();
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
				m.multi_material.multi_texture_count = 0;
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
				m.multi_material.LoadMultitexture(mat, root, world.GetMaterials());
				c->NotifySignatureChange(e);
			}
		}

		void ToolUi::LoadWorld(const std::string& world_file)
		{
			std::scoped_lock l(world.GetCoordinator()->GetSystem<RenderSystem>()->mutex);
			world.Stop();
			debug = nullptr;
			delete gui;
			delete post;

			world.Release();
			world.PreLoad(this);
			
			auto c = world.GetCoordinator();
			gui = new UI::GUI(context, width, height, world.GetCoordinator());
			post = new MainEffect(context, width, height);
			dof = new DOFProcess(context, width, height, world.GetCoordinator());
			motion = new MotionBlurEffect(context, width, height);

			world.Load(world_file);
			world.Init();
			world.Run(fps);

			post->SetNext(gui);
#if 0
			motion->SetNext(dof);
			dof->SetNext(gui);

			dof->SetAmplitude(2.0f);
			dof->SetFocus(6.0f);
			dof->SetEnabled(true);
#endif
			world.SetPostProcessPipeline(post);
		
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