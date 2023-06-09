/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include <Core\PhysicsCommon.h>
#include <Core\DXCore.h>
#include <World.h>
#include <Windows.h>
#include <Systems\RTSCameraSystem.h>
#include <GUI\GUI.h>
#include <GUI\Label.h>
#include <GUI\TextureWidget.h>
#include <GUI\ProgressBar.h>
#include <GUI\Button.h>

#include "GameComponents.h"
#include "GamePlayerSystem.h"
#include "DemoPostProcess.h"
#include "FireSystem.h"
#include "EnemySystem.h"
#include "GameCameraSystem.h"

using namespace std;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::UI;

/**
 * The main Game class. It's child of DXCore as the main game engine module and also EventListener to
 * subscribe to the engine events. You can also send your own events using the coordinator, just check how
 * is done it's done the engine (like in DXCore class).
 */
class GameDemoApplication : public DXCore, public ECS::EventListener
{
private:

	World world;
	DOPEffect* dop_fx = nullptr;
	PostGame* post_game = nullptr;
	UI::GUI* gui = nullptr;
	
	int target_fps = 60;
	std::shared_ptr<GamePlayerSystem> player_system;
	std::shared_ptr<EnemySystem> enemy_system;
	std::shared_ptr<GameCameraSystem> camera_system;

	ECS::Entity player = ECS::INVALID_ENTITY_ID;
	//This is the grass texture painted in the terrain
	static constexpr int NGRASS = 3;
	std::string root;
	bool game_over = false;
	bool retry_enable = false;
	ID3D11ShaderResourceView* grass[NGRASS] = {};
	float3 fireball_spawn_position = {};

public:
	GameDemoApplication(HINSTANCE hInstance) :DXCore(hInstance, "HotBiteDemoGame", 1920, 1080, true, true) {
		root = "..\\..\\..\\Tests\\DemoGame\\";
		//Initialize core DirectX
		InitWindow();
		CreateConsoleWindow(512, 512, 512, 512);

		InitDirectX();

		//Initialize world
		world.PreLoad(this);
		//Register our own systems and components in the engine
		world.RegisterComponent<GamePlayerComponent>();
		world.RegisterComponent<CreatureComponent>();
		world.RegisterComponent<FireComponent>();
		world.RegisterComponent<EnemyComponent>();
		world.RegisterComponent<TerrainComponent>();

		world.RegisterSystem<GamePlayerSystem>();
		world.RegisterSystem<FireSystem>();
		world.RegisterSystem<EnemySystem>();
		world.RegisterSystem<GameCameraSystem>();

		//Get pointer to the world coordinator, this coordinator manages the 
		//world systems, entities, components and events.
		ECS::Coordinator* c = world.GetCoordinator();
		//Event listeners needs to be initialized once a coordinator is available as 
		//the coordinator manages the events
		EventListener::Init(c);
		//Look for "EventId" in the engine to look for all the available events, no documentation available yet.
		//You can add your own events very easy, just check how it works in the engine.       
		AddEventListener(World::EVENT_ID_MATERIALS_LOADED, std::bind(&GameDemoApplication::OnMaterialsLoaded, this, std::placeholders::_1));
		AddEventListener(World::EVENT_ID_TEMPLATES_LOADED, std::bind(&GameDemoApplication::OnMaterialsLoaded, this, std::placeholders::_1));
		AddEventListener(GamePlayerSystem::EVENT_ID_PLAYER_DAMAGED, std::bind(&GameDemoApplication::OnPlayerDamaged, this, std::placeholders::_1));
		AddEventListener(GamePlayerSystem::EVENT_ID_PLAYER_DEAD, std::bind(&GameDemoApplication::OnPlayerDead, this, std::placeholders::_1));
		
		//Load the scene
		world.Load(root + "demo_game_scene.json");
		//Load the player animations as templates
		world.LoadTemplate("Assets\\archer\\archer.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_idle.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_jump.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_walk.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_run.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_attack.fbx", false, true);
		world.LoadTemplate("Assets\\archer\\archer_death.fbx", false, true);
		//Load zombie assets as templates
		world.LoadTemplate("Assets\\zombie\\zombie_tpose.fbx", false, true);
		world.LoadTemplate("Assets\\zombie\\zombie_idle.fbx", false, true);
		world.LoadTemplate("Assets\\zombie\\zombie_walk.fbx", false, true);
		world.LoadTemplate("Assets\\zombie\\zombie_death.fbx", false, true);
		world.LoadTemplate("Assets\\zombie\\zombie_attack.fbx", false, true);
		//Load troll assets as templates
		world.LoadTemplate("Assets\\troll\\troll_tpose.fbx", false, true);
		world.LoadTemplate("Assets\\troll\\troll_idle.fbx", false, true);
		world.LoadTemplate("Assets\\troll\\troll_walk.fbx", false, true);
		world.LoadTemplate("Assets\\troll\\troll_death.fbx", false, true);
		world.LoadTemplate("Assets\\troll\\troll_attack.fbx", false, true);
		//Init the world
		world.Init();

		grass[0] = HotBite::Engine::Core::LoadTexture(root + "\\Assets\\grass\\grass1.png");
		grass[1] = HotBite::Engine::Core::LoadTexture(root + "\\Assets\\grass\\grass2.png");
		grass[2] = HotBite::Engine::Core::LoadTexture(root + "\\Assets\\grass\\grass3.png");

		AddEventListener(World::EVENT_ID_UPDATE_BACKGROUND, std::bind(&GameDemoApplication::UpdateSystems, this, std::placeholders::_1));
		AddEventListener(RenderSystem::EVENT_ID_PREPARE_SHADER, std::bind(&GameDemoApplication::OnPrepare, this, std::placeholders::_1));

		//Load and set post effect chain to renderer
		int w = GetWidth();
		int h = GetHeight();
		//We have depth of field effect as post process
		dop_fx = new DOPEffect(context, w, h, 20.0f, 0.0f);
		//we add to the chain a GUI post process stage to render UI
		gui = new UI::GUI(context, w, h, world.GetCoordinator());
		//add our own post-chain effect, this one generates the damage player effect and the "you died" effect (see DemoPostEffect.hlsl)
		post_game = new PostGame(context, w, h, world.GetCoordinator());
		//Connect the post-chain effects
		dop_fx->SetNext(post_game);
		post_game->SetNext(gui);

		//Set the post-chain begin to the renderer
		world.SetPostProcessPipeline(dop_fx);

		//Setup and configure our systems (all registered systems are already instanciated during world init)
		enemy_system = c->GetSystem<EnemySystem>();
		camera_system = c->GetSystem<GameCameraSystem>();
		c->GetSystem<RenderSystem>()->SetParallaxShadow(true);

		//Init our own game PlayerSystem
		player_system = c->GetSystem<GamePlayerSystem>();
		player_system->SetCamera(c->GetSystem<CameraSystem>()->GetCameras().GetData()[0]);


		//Prepare player       
		SetUpPlayer();

		//Prepare zombies
		SetUpZombies();

		//Prepare trolls
		SetUpTrolls();

		//Prepare de screen GUI
		SetupGUI(gui);

		//Add the terrain component to all instances with name Terrain*
		//NOTE: "Terrain" is the name we give the terrain in the FBX file set in particle_test_scene.json,
		//same with any other entity name in the example, names are set in the FBX files 
		auto terrains = c->GetEntitiesByName("Terrain*");
		for (auto& terrain : terrains) {
			c->AddComponent<TerrainComponent>(terrain);
			c->NotifySignatureChange(terrain);
		}

		//Prepare fire balls and scene
		auto balls = c->GetEntitiesByName("Ball*");
		Core::MaterialData* particle_material = world.GetMaterials().Get("SmokeParticles");

		//Remove physics from water so it doesn't collide with objects
		auto water = c->GetEntityByName("Water");
		c->RemoveComponent<Physics>(water);
		c->NotifySignatureChange(water);

		//Lava physics is type "trigger" so it doesn't collide physically but gives contact information, so creatures can burn when touching the lava
		auto lavas = c->GetEntitiesByName("Lava*");
		for (auto& lava : lavas) {
			c->GetComponent<Components::Physics>(lava).collider->setIsTrigger(true);
			c->AddComponent<FireComponent>(lava, { .duration = MSEC_TO_NSEC(2000), .dps = 10.0f });
			std::shared_ptr<SmokeParticles> lava_particle = std::make_shared<SmokeParticles>();
			lava_particle->Init(c, lava, particle_material, 0.01f, 20, 0.2f, 10000, ParticlesData::PARTICLE_ORIGIN_FIXED_VERTEX, 0.3f, 0.9f, 0.0f, 10.0f);
			c->AddComponent<Particles>(lava, Particles{ "smoke", std::dynamic_pointer_cast<ParticlesData>(lava_particle) });
			c->NotifySignatureChange(lava);
		}

		//Setup fireballs
		for (auto& ball : balls) {
			if (ball != ECS::INVALID_ENTITY_ID) {
				fireball_spawn_position = c->GetComponent<Components::Transform>(ball).position;
				SetupFireBall(ball);
				c->NotifySignatureChange(ball);
			}
		}
		
		Components::Sky* sky = c->GetComponentPtr<Components::Sky>(c->GetEntityByName("Sky"));
		//We create a periodic timer to check keyboard input keys
		Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(1000000000 / 60, [this, sky](const Scheduler::TimerData& t) {
				if (!game_over) {
					//Add some keys to change component parameters and see how the scene changes
					if (GetAsyncKeyState('O') & 0x8000) {
						sky->cloud_density += 0.01f;
					}
					else if (GetAsyncKeyState('P') & 0x8000) {
						sky->cloud_density -= 0.01f;
					}
					else if (GetAsyncKeyState('U') & 0x8000) {
						sky->second_speed *= 2.0f;
					}
					else if (GetAsyncKeyState('I') & 0x8000) {
						sky->second_speed /= 2.0f;
					}
					else if (GetAsyncKeyState('L') & 0x8000) {
						auto renderer = world.GetCoordinator()->GetSystem<RenderSystem>();
						renderer->SetParallaxShadow(!renderer->GetParallaxShadow());
					}					
					else {
						//Check if player key is down
						player_system->CheckKeys();
					}
				}
				else if (retry_enable) {
					if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
						RestartGame();
					}
				}
				return true;
			});



		//We create a periodic timer to spawn a new fireballs in the scene evey 10 seconds.
		//It's important to create the fireballs in the rendering thread, as DirectX 11 requires only one thread
		Scheduler::Get(DXCore::MAIN_THREAD)->RegisterTimer(SEC_TO_NSEC(10), [this](const Scheduler::TimerData& td) {
				static int i = 0;
				SpawnFireBall(i++);
				return true;
			});

		//We create a periodic timer to manage the movement of the red and blue point lights, this is an example for 
		//moving entities periodically in the world.
		//The YOU DEAD text is smooth shown when game is over, this is done by changing the alpha value of the text
		Scheduler::Get(DXCore::BACKGROUND_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData& t) {
			ECS::Coordinator* c = world.GetCoordinator();
				float f = (float)t.total / 1000000000.0f;
				float a = sin(f) * 0.04f;
				float b = cos(f) * 0.04f;
				auto plights = c->GetEntitiesByName("Light*");
				for (const auto& l : plights) {
					Transform& t = c->GetComponent<Transform>(l);
					t.position.x += a;
					t.position.z += b;
					t.dirty = true;
				}
				if (game_over) {
					auto label = std::dynamic_pointer_cast<Label>(gui->GetWidget("dead"));
					float4 color = label->GetTextColor();
					if (color.w < 1.0f) {
						color.w += 0.005f;
						c->GetSystem<RenderSystem>()->mutex.lock();
						label->SetTextColor(color);
						label->SetFontSize(label->GetFontSize() + 0.1f);
						c->GetSystem<RenderSystem>()->mutex.unlock();
						if (color.w >= 1.0f) {
							retry_enable = true;
							std::dynamic_pointer_cast<Label>(gui->GetWidget("retry"))->SetVisible(true);
						}
					}
				}
				return true;
			});

		//Run the world at the desired FPS (if CPU/GPU can afford it...)
		world.Run(target_fps);
	}

	virtual ~GameDemoApplication() {
		for (int i = 0; i < NGRASS; ++i) {
			grass[i]->Release();
		}
		//Stop the world and delete raw pointers
		world.Stop();
		delete post_game;
		delete dop_fx;
		delete gui;
	}

	void RestartGame() {
		std::shared_ptr<Label> dead_label = std::dynamic_pointer_cast<Label>(gui->GetWidget("dead"));
		dead_label->SetFontSize(90.0f);
		dead_label->SetTextColor({ 0.8f, 0.0f, 0.0f, 0.0f });
		std::dynamic_pointer_cast<Label>(gui->GetWidget("retry"))->SetVisible(false);
		//Reset damage and dead time in our post process effect
		float time = 0.0f;
		post_game->SetVariable("damageTime", &time, sizeof(float));
		post_game->SetVariable("deadTime", &time, sizeof(float));
		//Restart game and respawn player
		retry_enable = false;
		game_over = false;
		camera_system->SetGameOver(false);
		player_system->Respawn();
	}

	//Our game needs to provide an official coordinator to other modules,
	//this is an abstract method from DXCore. Normally the coordinator of
	//the current world is returned.
	ECS::Coordinator* GetCoordinator() {
		return world.GetCoordinator();
	}

	void SetupGUI(UI::GUI* gui) {
		ECS::Coordinator* c = world.GetCoordinator();
		Components::Sky* sky = c->GetComponentPtr<Components::Sky>(c->GetEntityByName("Sky"));

		//Create the UI in the GUI post process stage
		std::shared_ptr<UI::Label> label1 = std::make_shared<UI::Label>(c, "label1", "", 16.0f,
			"Courier", DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		label1->SetBackgroundColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		label1->SetRect({ 0.90f, 0.8f }, 0.1f, 0.05f);
		label1->SetTextColor({ 1.0f, 1.0f, 1.0f, 1.0f });

		//Dynamic text is automatically updated internally by the widget, so we don't need to update it manually
		label1->SetDynamicText("Res: %dx%d\nTarget FPS: %d\nFPS: %d\n", width, height, target_fps, current_fps);
		gui->AddWidget(label1);

		std::shared_ptr<UI::Label> label2 = std::make_shared<UI::Label>(c, "label2", "", 12.0f,
			"Courier", DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		label2->SetBackgroundColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		label2->SetRect({ 0.90f, 0.1f }, 0.1f, 0.05f);
		label2->SetTextColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		gui->AddWidget(label2);

		CreatureComponent& player = c->GetComponent<CreatureComponent>(c->GetEntityByName("Player"));

		std::shared_ptr<UI::ProgressBar> health = std::make_shared<UI::ProgressBar>(c, "health", 0.0f, player.total_health);
		health->SetDynamicValue(&player.current_health);
		health->SetRect({ 0.3f, 0.95f }, 0.4f, 0.01f);
		health->SetBackGroundImage(root + "assets\\textures\\progress_bar_red.png");
		health->SetBarImage(root + "assets\\textures\\progress_bar_green.png");
		gui->AddWidget(health);

		//This is the "YOU DIED" message and retry button, hidden by default and shown when player is dead in the OnPlayerDead event
		std::shared_ptr<UI::Label> dead = std::make_shared<UI::Label>(c, "dead", "YOU DIED", 90.0f,
						"Times New Roman", DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_FONT_STYLE_NORMAL,
						DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		dead->SetBackgroundColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		dead->SetRect({ 0.0f, 0.0f }, 1.0f, 1.0f);
		dead->SetTextColor({ 0.8f, 0.0f, 0.0f, 0.0f });
		gui->AddWidget(dead);

		std::shared_ptr<UI::Label> retry = std::make_shared<UI::Label>(c, "retry", "Press ENTER to retry", 32.0f,
			"Times New Roman", DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		retry->SetBackgroundColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		retry->SetRect({ 0.0f, 0.0f }, 1.0f, 0.5f);
		retry->SetTextColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		retry->SetVisible(false);
		gui->AddWidget(retry);

		Scheduler::Get(DXCore::BACKGROUND3_THREAD)->RegisterTimer(1000000000, [this, sky, label2](const Scheduler::TimerData& t) {
				//This is a timer to update every second the information of the sky component (time of day, time speed and cloud density)
				ECS::Coordinator* c = world.GetCoordinator();
				std::lock_guard<std::recursive_mutex> l(c->GetSystem<RenderSystem>()->mutex);
				label2->SetText("Time: %02d:%02d:%02d\nTime Speed (U/I): x%0.2f\nClouds(O/P): %0.2f\n",
				(sky->current_minute / 60) % 24, sky->current_minute % 60,
				(int)sky->second_of_day % 60, sky->second_speed, sky->cloud_density);
				return true;
			});
	}

	//This method spawns a new fireball in the scene
	void SpawnFireBall(int id) {
		ECS::Coordinator* c = world.GetCoordinator();
		//NOTE: Always lock first renderer mutex before physics mutex to avoid interlock
		shared_ptr<RenderSystem> rs = c->GetSystem<RenderSystem>();
		rs->mutex.lock();
		physics_mutex.lock();
		if (id > 5) {
			//Maximum 5 fireballs in the scene to avoid avorload of particles
			//we can just remove the entity of the old fireball
			std::string name2 = "_Ball_" + std::to_string(id - 6);
			ECS::Entity ball = c->GetEntityByName(name2);
			if (ball != ECS::INVALID_ENTITY_ID) {
				c->DestroyEntity(ball);
			}
		}
		{
			std::string name = "_Ball_" + std::to_string(id);
			ECS::Entity ball = c->CreateEntity(name);

			c->AddComponent<Base>(ball, { .name = name, .id = ball, .draw_method = eDrawMethod::DRAW_SCREEN });
			c->AddComponent<Bounds>(ball, c->GetConstComponent<Bounds>(c->GetEntityByName("Ball")));
			c->AddComponent<Mesh>(ball);
			c->AddComponent<Lighted>(ball);
			c->AddComponent<Material>(ball, { .data = world.GetMaterials().Get("Ball") });
			c->AddComponent<Transform>(ball, { .position = fireball_spawn_position });
			c->AddComponent<Physics>(ball, Physics{});
			Mesh& m = c->GetComponent<Mesh>(ball);
			Bounds& b = c->GetComponent<Bounds>(ball);
			Physics& p = c->GetComponent<Physics>(ball);
			Transform& t = c->GetComponent<Transform>(ball);
			m.SetData(world.GetMeshes().Get("Ball"));
			p.type = reactphysics3d::BodyType::DYNAMIC;
			p.shape = Physics::SHAPE_SPHERE;
			p.Init(world.GetPhysicsWorld(), p.type, nullptr, b.bounding_box.Extents, t.position, t.scale, t.rotation, p.shape);
			SetupFireBall(ball);

			c->NotifySignatureChange(ball);
		}
		physics_mutex.unlock();
		rs->mutex.unlock();
	}

	void SetupFireBall(ECS::Entity ball) {

		ECS::Coordinator* c = world.GetCoordinator();

		Core::MaterialData* particle_material = world.GetMaterials().Get("SmokeParticles");
		Core::MaterialData* fire_particle_material = world.GetMaterials().Get("FireParticles");
		Core::MaterialData* sparks_particle_material = world.GetMaterials().Get("SparksParticles");

		Physics& p = c->GetComponent<Physics>(ball);
		reactphysics3d::Material* m = p.GetMaterial();
		if (m != nullptr) {
			//Set the bool physics properties
			m->setBounciness(0.3f);
			m->setFrictionCoefficient(0.3f);
			//Allow free rotation
			p.body->setAngularLockAxisFactor(rp3d::Vector3(1.0f, 1.0f, 1.0f));
			//Add an initial movement to the ball
			p.body->applyLocalForceAtCenterOfMass({ 10.0f, 10.0f,  10.0f });
		}

		//Init the particle component of the fire balls.
		//When using particles we have 2 options: Defining child classes of ParticlesData class or using function callback to implement
		//the particle movement.
		std::shared_ptr<SmokeParticles> smoke_particle = std::make_shared<SmokeParticles>();
		smoke_particle->Init(c, ball, particle_material, 0.05f, 50, 1.0f, 5000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.3f, 0.3f, 0.0f, 1.0f);
		std::shared_ptr<FireParticles> fire_particle = std::make_shared<FireParticles>();
		fire_particle->Init(c, ball, fire_particle_material, 0.1f, 30, 1.0f, 1000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.7f, 0.3f, 0.0f, 0.0f, float3(0.0f, 0.8f, 0.0f));

		//This is an example of a generic particle where we define a lambda function to implement
		//the particle movement instead of using a child class, like we did with FireParticle or SmokeParticle.
		std::shared_ptr<ParticlesData> sparks_particle = std::make_shared<ParticlesData>([](ParticlesData& particles, uint64_t elpased_nsec, uint64_t total_nsec) {
				float t = (float)(total_nsec) / 1000000000.0f;
				auto& data = particles.particles_info.GetData();
				auto& vertices = particles.particles_vertex.GetData();
				auto& indices = particles.particles_indices.GetData();
				for (int i = 0; i < data.size(); ++i) {
					Particle& p = data[i];
					ParticleVertex& v = vertices[i];
					float n = (float)vertices[i].Id;
					//Update the particle position	
					v.Position.x += 0.01f * sin(t * 2.0f + n);
					v.Position.z += 0.01f * cos(t * 2.5f + n);
					v.Position.y += 0.05f * v.Life;
				}
				//Add to buffer
				particles.vertex_buffer->FlushMesh(vertices, indices);
			});
		sparks_particle->Init(c, ball, sparks_particle_material, 0.5f, 50, 0.01f, 3000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.0f, 0.3f, 0.0f, 1.0f);

		//Now we add all the particles to the particle component of the entity
		//and add the component to the entity, they are rendered in alphabetical order
		//so we can control the order of rendering by naming the particles accordingly.
		Particles part;
		part.data.Insert("1_sparks", sparks_particle);
		part.data.Insert("2_fire_core", fire_particle);
		part.data.Insert("3_smoke", smoke_particle);
		c->AddComponent<Particles>(ball, part);
		c->AddComponent<FireComponent>(ball, FireComponent{ .duration = SEC_TO_NSEC(5), .dps = 10.0f });
	}

	void OnMaterialsLoaded(ECS::Event& e) {
		for (MaterialData& m : world.GetMaterials().GetData()) {
			if (m.name == "Rocks") {
				//We use our own shaders for the terrain, to render plants, this way you can set your own
				//shaders to any material. Shaders needs to be based on the engine shaders, so take
				//the engine default shader, copy it to your folder, modify it and set it to the material
				m.shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("TerrainPS.cso");
				m.shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("TerrainVS.cso");
				m.shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>("TerrainGS.cso");
				m.shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>("TerrainDS.cso");
			}
		}
	}

	//This event is triggered during rendering every time a shader is prepared to render the entity as we subsribed to RenderSystem::EVENT_ID_PREPARE_SHADER
	//we set the grass textures to be available in our own TerrainPS shader, if it's another shader SetShaderResourceViewArray does nothing
	void OnPrepare(ECS::Event& ev) {
		HotBite::Engine::Core::SimplePixelShader* ps = std::get<HotBite::Engine::Core::SHADER_KEY_PS>(ev.GetParam<HotBite::Engine::Core::ShaderKey>(HotBite::Engine::Systems::RenderSystem::EVENT_PARAM_SHADER));
		if (ps != nullptr) {
			ps->SetShaderResourceViewArray("grassTexture[0]", grass, NGRASS);
		}
	}

	void OnPlayerDamaged(ECS::Event& e) {
		if (e.GetEntity() == player) {
			//We get notification every time the player is damaged, and we set a variable in the post-effect shader to perform a damage effect
			float time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;
			post_game->SetVariable("damageTime", &time, sizeof(float));
		}
	}

	void OnPlayerDead(ECS::Event& e) {
		//Activate the damage effect in the post-chain
		float time = (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f;
		post_game->SetVariable("deadTime", &time, sizeof(float));
		game_over = true;
		camera_system->SetGameOver(true);
	}

	//Event World::EVENT_ID_UPDATE_BACKGROUND received every update period loop of the background thread,
	//we use this event to update our own systems
	void UpdateSystems(ECS::Event& ev) {
		player_system->Update(0, 0);
		enemy_system->Update(0, 0);
	}
	
	//Here we setup the player entity, in the main scene FBX file it's just a square
	//so we change it to the real player.
	void SetUpPlayer() {
		ECS::Coordinator* c = world.GetCoordinator();
		//Get the player entity, "Player" is the name of the entity in the FBX file
		player = c->GetEntityByName("Player");
		//This entity is the player, managed by the PlayerSystem, you want to create your own system
		//to manage the animations, keys, stats, etc...
		c->AddComponent<Components::Player>(player);
		c->AddComponent<GamePlayerComponent>(player, { .attack_frame = 5,
													   .damage = 10 });

		c->AddComponent<CreatureComponent>(player, { .current_health = 100.0f,
													 .total_health = 100.0f ,
												     .animations = {{CreatureAnimations::ANIM_IDLE, "archer_idle" },
																	{CreatureAnimations::ANIM_JUMP, "archer_jump" },
																	{CreatureAnimations::ANIM_ATTACK, "archer_attack" },
																	{CreatureAnimations::ANIM_WALK, "archer_walk" },
																	{CreatureAnimations::ANIM_RUN, "archer_run" },
																	{CreatureAnimations::ANIM_DEAD, "archer_death" } } });
		AddParticlesToEntity(player);
		//Get its current mesh, material and transform components and modify them to
		//set the Mannequin mesh and materials, we change the scale to adapt it correctly
		//to the scene
		Components::Mesh& mesh = c->GetComponent<Components::Mesh>(player);
		Components::Material& mat = c->GetComponent<Components::Material>(player);
		Components::Transform& transform = c->GetComponent<Components::Transform>(player);
		CreatureComponent& creature = c->GetComponent<CreatureComponent>(player);
		mesh.SetCoordinatorInfo(player, c);
		mesh.SetData(world.GetMeshes().Get("archer"));
		for (auto& anim : creature.animations) {
			mesh.GetData()->AddSkeleton(*world.GetSkeletons().Get(anim.second));			
		}
		mesh.SetAnimation(creature.animations[CreatureAnimations::ANIM_IDLE]);

		mat.data = world.GetMaterials().Get("ArcherMaterial");
		transform.scale = { 0.02f, 0.02f, 0.02f };
		//After the components have been changed we notify a change in the coordinator
		//as some systems can need to refresh their internal data structures (like the renderer, 
		//that uses the material and shaders of the entity to generate a tree).
		auto cam = c->GetSystem<CameraSystem>()->GetCameras().GetData()[0];
		cam.base->parent = player;
		c->NotifySignatureChange(player);
	}
	
	void SetUpZombies() {
		CreatureComponent cr = {.current_health = 20.0f,
						.total_health = 20.0f,
						.mesh_name = "parasiteZombie",
					    .material_name = "ZombieMaterial",
						.scale = { 0.02f, 0.02f, 0.02f },
						.animations = { {CreatureAnimations::ANIM_IDLE, "zombie_idle" },
									{CreatureAnimations::ANIM_ATTACK, "zombie_attack" },
									{CreatureAnimations::ANIM_WALK, "zombie_walk" },
									{CreatureAnimations::ANIM_DEAD, "zombie_death" } } };
		EnemyComponent en = { .speed = 0.2f,
							.range = 500.0f,
							.damage = 20.0f,
							.attack_range = 10.0f,
							.attack_frame = 5,
							.attack_period = MSEC_TO_NSEC(2666) };
		SetUpEnemies("Zombie*", cr, en);
	}

	void SetUpTrolls() {
		CreatureComponent cr = { .current_health = 80.0f,
						.total_health = 80.0f,
						.mesh_name = "troll",
						.material_name = "TrollMaterial",
						.scale = { 0.025f, 0.025f, 0.025f },
						.animations = { {CreatureAnimations::ANIM_IDLE, "troll_idle" },
									{CreatureAnimations::ANIM_ATTACK, "troll_attack" },
									{CreatureAnimations::ANIM_WALK, "troll_walk" },
									{CreatureAnimations::ANIM_DEAD, "troll_death" } } };
		EnemyComponent en = { .speed = 1.5f,
							.range = 500.0f,
							.damage = 50.0f,
							.attack_range = 40.0f,
							.attack_frame = 5,
							.attack_period = MSEC_TO_NSEC(2666) };
		SetUpEnemies("Troll*", cr, en);
	}

	void SetUpEnemies(const std::string& name, const CreatureComponent& creature, const EnemyComponent& enemy) {
		ECS::Coordinator* c = world.GetCoordinator();
		//Get the zombies entities
		std::list<ECS::Entity> enemies = c->GetEntitiesByName(name);
		for (ECS::Entity e : enemies) {
			c->AddComponent<CreatureComponent>(e, creature);
			c->AddComponent<EnemyComponent>(e, enemy);
			AddParticlesToEntity(e);
			//Get its current mesh, material and transform components and modify them to
			//set the zombie mesh and materials, we change the scale to adapt it correctly
			//to the scene
			Components::Base& base = c->GetComponent<Components::Base>(e);
			Components::Mesh& mesh = c->GetComponent<Components::Mesh>(e);
			Components::Material& mat = c->GetComponent<Components::Material>(e);
			Components::Transform& transform = c->GetComponent<Components::Transform>(e);
			CreatureComponent& creature = c->GetComponent<CreatureComponent>(e);
			base.draw_method = DRAW_SCREEN;
			mesh.SetCoordinatorInfo(e, c);
			mesh.SetData(world.GetMeshes().Get(creature.mesh_name));
			for (auto& anim : creature.animations) {
				mesh.GetData()->AddSkeleton(*world.GetSkeletons().Get(anim.second));
			}
			mesh.SetAnimation(creature.animations[CreatureAnimations::ANIM_IDLE]);
			mat.data = world.GetMaterials().Get(creature.material_name);
			transform.scale = creature.scale;
			c->NotifySignatureChange(e);
		}
	}

	//We add particles to every creature, the particles are use during burning, see FireSystem
	void AddParticlesToEntity(ECS::Entity e) {
		ECS::Coordinator* c = world.GetCoordinator();
		c->AddComponent<Particles>(e);
		Core::MaterialData* smoke_particle_material = world.GetMaterials().Get("SmokeParticles");
		Core::MaterialData* fire_particle_material = world.GetMaterials().Get("FireParticles");
		Core::MaterialData* sparks_particle_material = world.GetMaterials().Get("SparksParticles");

		//Add fire particles and smoke to the player
		std::shared_ptr<SmokeParticles> smoke_particle = std::make_shared<SmokeParticles>();
		smoke_particle->Init(c, e, smoke_particle_material, 0.0f, 30, 0.7f, 3000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.3f, 0.3f, 0.0f, 1.0f, { 0.0f, 1.0f, 0.0f });
		std::shared_ptr<FireParticles> fire_particle = std::make_shared<FireParticles>();
		fire_particle->Init(c, e, fire_particle_material, 0.0f, 100, 0.3f, 1000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.7f, 0.3f);
		std::shared_ptr<SparksParticles> sparks_particle = std::make_shared<SparksParticles>();
		sparks_particle->Init(c, e, sparks_particle_material, 0.0f, 50, 0.01f, 3000, ParticlesData::PARTICLE_ORIGIN_RNG_VERTEX, 0.0f, 0.3f, 0.0f, 1.0f);
		Particles& p = c->GetComponent<Particles>(e);
		p.data.Insert("1_sparks", sparks_particle);
		p.data.Insert("2_fire_core", fire_particle);
		p.data.Insert("3_smoke", smoke_particle);
	}
};

//Windows main entry point
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	GameDemoApplication test(hInstance);
	test.Run();
}
