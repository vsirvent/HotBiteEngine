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

#pragma once

#include <Core/Json.h>
#include <reactphysics3d/reactphysics3d.h>

#include "Defines.h"
#include <memory>
#include <set>
#include <map>
#include <Loader\FBXLoader.h>
#include <ECS/Coordinator.h>
#include <ECS/ComponentRegistry.h>
#include <Systems\CameraSystem.h>
#include <Systems\DirectionalLightSystem.h>
#include <Systems\PhysicsSystem.h>
#include <Systems\PlayerSystem.h>
#include <Systems\PointLightSystem.h>
#include <Systems\RenderSystem.h>
#include <Systems\StaticMeshSystem.h>
#include <Systems\SplatCloudSystem.h>
#include <Systems\SkySystem.h>
#include <Systems\ParticleSystem.h>
#include <Systems\AnimationSystem.h>
#include <Systems\AudioSystem.h>
#include <Components\Base.h>
#include <Components\Camera.h>
#include <Components\Lights.h>
#include <Components\Physics.h>
#include <Core\DXCore.h>
#include <Core\Material.h>
#include <Core\Mesh.h>
#include <Core\SplatCloud.h>
#include <Core\Utils.h>
#include <ECS\Types.h>
#include <Network\Commons.h>

namespace HotBite {
	namespace Engine {
		class World: public ECS::IEventSender, public ECS::EventListener {
		public:
			static inline ECS::EventId EVENT_ID_MATERIALS_LOADED = ECS::GetEventId<World>(0x00);
			static inline ECS::EventId EVENT_ID_MESHES_LOADED = ECS::GetEventId<World>(0x01);
			static inline ECS::EventId EVENT_ID_SHAPES_LOADED = ECS::GetEventId<World>(0x02);
			static inline ECS::EventId EVENT_ID_TEMPLATES_LOADED = ECS::GetEventId<World>(0x03);
			static inline ECS::EventId EVENT_ID_UPDATE_MAIN = ECS::GetEventId<World>(0x04);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND = ECS::GetEventId<World>(0x05);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND2 = ECS::GetEventId<World>(0x06);
			static inline ECS::EventId EVENT_ID_UPDATE_BACKGROUND3 = ECS::GetEventId<World>(0x07);
			static inline ECS::EventId EVENT_ID_UPDATE_PHYSICS = ECS::GetEventId<World>(0x08);

			// What one imported asset file brought into the world, by the names those
			// assets are registered under. This is the inventory an editor lists under
			// a model, and the reason a model is worth being a thing of its own: an
			// .fbx is a *source of assets*, and which assets it contributed is not
			// otherwise recoverable once everything has been merged into the world's
			// flat mesh/material/skeleton collections.
			struct ModelAssets {
				std::string file;                        // as loaded, level-relative or absolute
				bool triangulate = false;
				std::vector<std::string> meshes;
				std::vector<std::string> materials;
				std::vector<std::string> animation_sets; // skeletons, i.e. clip sets
				// Gaussian splat clouds, for a .ply. Mutually exclusive with the three
				// above in practice: a .ply carries a cloud and nothing else, an .fbx
				// carries everything else and no cloud.
				std::vector<std::string> splat_clouds;
			};

			// A mesh this world produced by simplifying another one, rather than by
			// importing it (GenerateMeshLod). The triple is what a level writes into
			// its "generated_meshes" array and what a load reads back, and it is
			// deliberately the *recipe* and not just the file: the file can be missing,
			// stale or written by another build, and a recipe can always be run again.
			struct GeneratedMesh {
				std::string name;   // what it is registered as, and what a chain names
				std::string source; // the mesh it was made from
				float ratio = 0.5f; // the share of the source's vertices it aimed for
				std::string file;   // where the geometry is cached, assets-path relative
			};

			// Where GenerateMeshLod caches its geometry, under the assets path. One
			// folder rather than beside each model: a generated mesh belongs to no
			// .fbx, and a folder of them is the thing to delete when they should all
			// be rebuilt.
			static inline const char* GENERATED_MESH_DIR = "GeneratedMeshes";

		protected:
			std::atomic<uint64_t> current_server_nsec = 0;
			std::atomic<uint64_t> current_background_thread_nsec = 0;
			std::atomic<uint64_t> current_physics_thread_nsec = 0;
			uint64_t physics_thread_period = 1000000000 / 60;
			uint64_t background_thread_period = 1000000000 / 60;
			uint64_t tick_period = 0;
			bool lockstep_init = false;
			bool lockstep_sync = false;
			std::atomic<bool> physics_paused = false;
			ECS::Event lockstep_tick_ev{ this, Network::LockStep::Command::EVENT_ID_NEW_SERVER_COMMAND };
			
			Core::VertexBuffer<Core::Vertex>* vertex_buffer = nullptr;
			BVHBuffer* bvh_buffer = nullptr;
			reactphysics3d::PhysicsWorld* phys_world = nullptr;
			Core::DXCore* dx_core;

			//Absolute path of assets location
			std::string path;
			std::unordered_map<std::string, std::set<ECS::Entity>> template_entities;
			//Imported asset files, by model name (the file stem). See the Models block
			//in the public section for what a model is and why it is not a template.
			std::unordered_map<std::string, std::set<ECS::Entity>> model_entities;
			std::map<std::string, ModelAssets> model_assets;
			//Component blocks of the templates that were authored rather than loaded
			//from an FBX, by template name. See CreateTemplate for what this holds and
			//why it is kept beside the template entity instead of on it.
			std::map<std::string, nlohmann::json> authored_templates;
			//The parts of the composed templates, by template name (see the Composed
			//templates block in the public section). Kept beside the components rather
			//than inside them because a part is not a component of the root: it is
			//another whole object, spawned as an entity of its own.
			std::map<std::string, nlohmann::json> template_parts;
			//Clone entity name -> root source entity name, so Init() can resolve the
			//collision ShapeData of clones created during Load() (shapes are keyed by
			//the original FBX entity name).
			std::unordered_map<std::string, std::string> clone_shape_alias;
			//Named layer stacks, and the .mat file each came from - the same
			//name/origin/retired triple materials keep, for the same reasons.
			std::map<std::string, Core::MultiMaterialData> multi_materials;
			std::map<std::string, std::string> multi_material_origin;
			std::set<std::string> removed_multi_materials;
			// Components a level record explicitly stripped from an entity, by entity id.
			// Init() hands every mesh entity a Physics component when it has none, which
			// would silently undo a "remove": ["Physics"] the loader had just honoured -
			// so it asks here first. Keyed by id rather than name because renames are
			// applied in the same pass that records these.
			std::unordered_map<ECS::Entity, std::set<std::string>> removed_components;
			Core::FlatMap<std::string, Core::MaterialData> materials{ ECS::MAX_ENTITIES };
			//Where each material came from, so an editor can write it back to the file
			//that declared it instead of guessing. `material_files` is keyed by the .mat
			//path as the level referenced it (relative to the assets path), because that
			//is the form the level's "material_files" array round-trips; the value is the
			//texture root that file declares, needed to make texture paths relative again.
			//Materials with no entry here came from an FBX or from code and belong to no
			//file until something assigns them one.
			std::map<std::string, std::string> material_files;
			std::map<std::string, std::string> material_origin; //material name -> .mat path
			//Materials retired via RemoveMaterial. Their data is deliberately still in
			//`materials` (see RemoveMaterial) but they must stay out of every listing and
			//every saved file.
			std::set<std::string> removed_materials;
			Core::FlatMap<std::string, Core::MeshData> meshes{ ECS::MAX_ENTITIES };
			//The meshes in there that this world generated rather than imported, in
			//creation order. See GeneratedMesh and GenerateMeshLod.
			std::vector<GeneratedMesh> generated_meshes;
			//Gaussian splat clouds, keyed by asset name. A peer of `meshes`: shared,
			//immutable geometry that a SplatCloud component points at. Unlike a MeshData,
			//each one owns its own GPU buffer - splats never reach the input assembler, so
			//there is no world-wide vertex buffer for them to be offsets into.
			Core::FlatMap<std::string, Core::SplatCloudData> splat_clouds{ ECS::MAX_ENTITIES };
			Core::FlatMap<std::string, Core::ShapeData> shapes{ ECS::MAX_ENTITIES };
			Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>> animations{ ECS::MAX_ENTITIES };

			ECS::Coordinator* coordinator = nullptr;
			ECS::Coordinator* templates_coordinator = nullptr;

			std::shared_ptr<Systems::CameraSystem> camera_system;
			std::shared_ptr<Systems::DirectionalLightSystem> dirlight_system;
			std::shared_ptr<Systems::PhysicsSystem> physics_system;
			std::shared_ptr<Systems::PointLightSystem> pointlight_system;
			std::shared_ptr<Systems::RenderSystem> render_system;
			std::shared_ptr<Systems::StaticMeshSystem> static_mesh_system;
			std::shared_ptr<Systems::SplatCloudSystem> splat_cloud_system;
			std::shared_ptr<Systems::SkySystem> sky_system;
			std::shared_ptr<Systems::AnimationMeshSystem> animation_mesh_system;
			std::shared_ptr<Systems::ParticleSystem> particle_system;
			std::shared_ptr<Systems::AudioSystem> audio_system;
			bool running = false;
			bool init = false;
			//Init() has run: entities created after this point (editor spawns/clones)
			//must set up their own physics bodies instead of relying on the Init() pass.
			bool scene_init = false;
			//A mesh's vertices changed on the CPU side and the GPU buffers no longer
			//match; see SetMeshSmooth / FlushMeshBuffers.
			bool mesh_buffers_dirty = false;
			std::unordered_map<std::string, std::shared_ptr<ECS::System>> systems_by_name;
			std::list<int> run_timer_ids[Core::DXCore::NTHREADS];
			std::set<std::string> loaded_files;

			void OnLockStepTick(ECS::Event& ev);
			void SetupCoordinator(ECS::Coordinator* c);
			void LoadSky(const nlohmann::json& sky_info);
			// Registers geometry produced here (rather than imported) as a mesh asset
			// under `name`. The skeleton and the smoothing flag come from the mesh it
			// was made from: a level of a skinned model has to be skinned to the same
			// skeleton to be usable as one at all (CompatibleSkinning), and shading
			// that did not match the source's would show up as the model changing
			// appearance when it drops a level.
			Core::MeshData* InstallGeneratedMesh(const std::string& name,
				const std::vector<Core::Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				const std::vector<uint32_t>& smooth_groups,
				std::shared_ptr<Core::Skeleton> skeleton, bool smooth);
			// The level's "generated_meshes" section: one {name, source, ratio, file}
			// recipe per mesh (see GeneratedMesh). Each is read from its cache file
			// when that file still matches the source, and simplified again when it
			// does not. Runs after the models and the "meshes" section - it needs the
			// source meshes - and before any template is created, because a template's
			// Mesh block may name one of these in its LOD chain.
			void LoadGeneratedMeshes(const nlohmann::json& entries);
			std::set<ECS::Entity> LoadFBX(const std::string& file, bool triangulate, bool relative,
							Core::FlatMap<std::string, Core::MaterialData>& materials,
							Core::FlatMap<std::string, Core::MeshData>& meshes,
							Core::FlatMap<std::string, Core::ShapeData>& shapes,
							ECS::Coordinator* c, Core::VertexBuffer<Core::Vertex>* vb, bool use_animation_names = false);
			void LoadInstances(const nlohmann::json& instances_json);
			static void ParsePhysicsJson(const nlohmann::json& physics_json, Components::Physics& physics);
			// The two halves of SpawnInstance. SpawnTemplateEntities creates the entities
			// one template describes (one per renderable FBX node, or the single entity of
			// an authored one) and knows nothing about parts; SpawnComposed wraps it and
			// walks the part tree, carrying the chain of templates already being spawned
			// so a cycle is refused rather than followed.
			ECS::Entity SpawnTemplateEntities(const std::string& name, const std::string& template_name,
							const float3& position, const float4& rotation, const float3& scale,
							const std::string& material_name, const nlohmann::json* physics_json,
							std::vector<ECS::Entity>* out_parts);
			// Where an entity actually is once its parents are composed in, which for an
			// attached part is not what its own Transform says (that holds the offset).
			// False - and the entity's own pose - when it has no parent. Anything placing
			// a rigid body has to go through this, or a part's collider sits at the
			// offset, i.e. wherever that offset happens to point from the world origin.
			bool ComposedWorldPose(ECS::Entity e, float3& position, float4& rotation) const;
			void CollectInstanceNames(const std::string& instance_name, const std::string& template_name,
							std::set<std::string>& chain, int depth, std::vector<std::string>& out);
			ECS::Entity SpawnComposed(const std::string& name, const std::string& template_name,
							const float3& position, const float4& rotation, const float3& scale,
							const std::string& material_name, const nlohmann::json* physics_json,
							std::vector<ECS::Entity>* out_parts, std::set<std::string>& chain, int depth);

		public:

			World();
			~World();

			virtual bool PreLoad(Core::DXCore* dx);
			virtual bool Release();
			virtual void SetLockStepSync(bool enabled);
			virtual bool GetLockStepSync(void);
			// Pauses stepping of the physics simulation (and the body->Transform
			// write-back) while every other system keeps running. Meant for editors:
			// dynamic bodies must hold still while the scene is authored, or gravity
			// keeps undoing hand-placed transforms. Bodies keep their poses; resuming
			// continues the simulation from wherever they currently are.
			virtual void SetPhysicsPause(bool paused);
			virtual bool GetPhysicsPause(void) const;
			virtual bool Load(const std::string& scene_file, float* progress = nullptr, std::function<void(float)> OnLoadProgress = nullptr, float progress_unit = 1.0f);
			virtual void Init();
			virtual void SetPostProcessPipeline(Core::PostProcess* pipeline);
			// auto_render: when false, skips registering the main-thread render timer
			// (RenderSystem::Update, i.e. Clear/Draw/Present) while still starting the
			// physics/background/audio timers. Lets a host that hasn't loaded any scene
			// content yet (e.g. an editor showing only a project picker UI) drive its own
			// render tick on its own schedule instead, without waiting on Load()/Init().
			virtual void Run(int render_fps, int background_fps = 0, int physics_fps = 0, bool auto_render = true);
			virtual void Stop();
			// --- Models ---------------------------------------------------------------
			//
			// A model is an imported asset file (.fbx): the meshes, materials, collision
			// shapes and animation clips it carries. It is an *asset*, not an object -
			// loading one places nothing in the scene and creates no template.
			//
			// That separation is the whole point. An .fbx is not one concept: an artist
			// exports a character mesh from one file and its walk cycle from another, and
			// a level's asset list ends up holding meshes, animation-only files and sky
			// geometry side by side. Treating each of them as a placeable object (which
			// is what LoadTemplate used to do) meant "walk" and "sky" showed up as things
			// you could drop into the world, most of which spawn nothing at all.
			//
			// So: models bring assets in, templates name a *kind of object* built out of
			// those assets (see the block below), and instances are templates placed in a
			// scene. The three are separate registries, and a level file lists models in
			// "models" and templates in "templates".
			// `name` is what the model registers under, and it is only a registry key:
			// the meshes, materials and clips inside the file keep their own names.
			// Empty means the file stem, which is what every caller wanted before a
			// model could be named - two files of the same stem in different folders
			// used to be one model, and an imported "untitled.fbx" could only ever be
			// called "untitled".
			virtual void LoadModel(const std::string& model_file, bool triangulate, bool relative,
							bool use_animation_names = false, const std::string& name = "");
			virtual bool IsModelLoaded(const std::string& name) const;
			// Unregisters a model: it stops being listed, stops being saved with the
			// level, and its FBX nodes are destroyed.
			//
			// What it deliberately does NOT do is unload the meshes, materials and
			// animation clips the file contributed. They live in flat, shared
			// collections that anything may hold a pointer into - a template's Mesh, an
			// entity's Material - and erasing from those relocates the neighbours, so
			// this would dangle assets belonging to other models (the same reason
			// RemoveMaterial retires instead of erasing). They stay for the session and
			// are simply not there on the next load, which is the contract
			// RemoveTemplate already has.
			virtual bool RemoveModel(const std::string& name);
			// Every loaded model name, sorted.
			virtual std::vector<std::string> ListModels() const;
			// What `name` brought in, or null when no such model was loaded.
			virtual const ModelAssets* GetModelAssets(const std::string& name) const;
			// The entities an imported model registered in the templates coordinator -
			// one per FBX node. They are what a legacy level's instances resolve to (see
			// GetTemplateEntities) and what "make a template out of this model" reads.
			virtual const std::set<ECS::Entity>& GetModelEntities(const std::string& name);
			// Kept for games that load their own asset files. Exactly LoadModel, under
			// the name it had when an imported file was also a template.
			virtual void LoadTemplate(const std::string& template_file, bool triangulate, bool relative, bool use_animation_names = false);
			virtual void LoadMaterialFiles(const nlohmann::json& materials_info, const std::string& path);
			virtual void LoadMaterialsNode(const nlohmann::json& materials_info,
										  const std::string& texture_path);
			// The entities behind a template name. A name that is not a template but is
			// a loaded *model* resolves to that model's entities: a level written before
			// models and templates were separate places instances directly from an .fbx,
			// and this is what keeps those levels loading (and what makes an editor
			// migrate them by saving). New content never relies on it - the editor's
			// "Create Template" turns a model into a real template first.
			virtual const std::set<ECS::Entity>& GetTemplateEntities(const std::string& template_name);
			virtual bool IsTemplateLoaded(const std::string& template_name);

			// --- Templates ----------------------------------------------------------
			//
			// A template is one concept: a named entity definition - a component block
			// with values (mesh, material, animation library, physics, a game's own
			// components) - that SpawnInstance stamps into a scene as often as needed.
			// It is the only placeable kind of thing.
			//
			// It is stored in two halves, and they deliberately hold different things:
			//
			//   - a template *entity* in the templates coordinator, carrying only what
			//     SpawnInstance clones (Base/Transform/Bounds/Mesh/Material/Lighted).
			//     Components that own engine resources are kept off it on purpose: a
			//     Physics block applied to a template entity would create a rigid body
			//     in the physics world for something that is not in the scene at all.
			//
			//   - the full component JSON, applied by SpawnInstance to every entity
			//     spawned from the template. That is what carries Physics, the selected
			//     animation and unknown-to-the-engine game components through to the
			//     instance, and it is applied *before* the instance's own overrides so a
			//     per-instance block still wins.
			//
			// Creating a template under a name that already names one replaces its
			// definition. A model of the same name is not a conflict - the two live in
			// separate registries, and "the troll template built from the troll model"
			// is the normal case - but the template wins wherever a name is resolved as
			// a placeable thing.
			virtual bool CreateTemplate(const std::string& name, const nlohmann::json& components,
							std::string& error);

			// --- Composed templates -------------------------------------------------
			//
			// A template may also declare *parts*: other templates, placed relative to
			// its root. That is what makes "a troll carrying this sword", "these six
			// pieces are one house" or "this armour goes on that body" one placeable
			// thing instead of an assembly to be redone by hand in every level.
			//
			//   [ { "name":     "sword",        // unique within this template
			//       "template": "iron_sword",   // any other template, by name
			//       "attach":   true,           // parent to the root and follow it
			//       "bone":     "hand_r",       // ride a joint of the root's skeleton
			//       "position"/"rotation"/"scale",   // relative to the root (or the bone)
			//       "components": { ... } } ]   // overrides on top of that template
			//
			// A part is a *reference*, resolved when an instance is spawned. Nothing is
			// copied, so one sword template can be a part of any number of composed
			// templates and editing it reaches all of them; and since resolution happens
			// at spawn, the order templates are declared in never matters. A part may
			// itself be composed - the tree is walked recursively, with a cycle guard,
			// because a template that contained itself would spawn until the process
			// died.
			//
			// `attach` is the difference between an object and a pile:
			//
			//   true  - the part is parented to the root (Base::parent, plus
			//           Base::parent_bone for a socket), so moving the root moves it and
			//           an animated root carries it. Its Transform holds the *offset*.
			//   false - the part is spawned at the composed world pose as an entity of
			//           its own, linked to nothing. Right for scenery whose pieces each
			//           carry their own collision and never move again.
			//
			// Physics follows from that, and this is the rule the editor states too: a
			// bone-attached part gets no rigid body (its pose changes every frame, so a
			// collider would be stale the instant it was made), an attached part with a
			// DYNAMIC or KINEMATIC body has it dropped (PhysicsSystem writes body poses
			// straight into the Transform and would silently overwrite the attachment),
			// and an attached STATIC body is created at the composed world pose. A
			// composed object that *moves* must carry its collision on the root.
			virtual const nlohmann::json* GetTemplateParts(const std::string& name) const;
			virtual bool IsComposedTemplate(const std::string& name) const;
			// Registers a template and its parts in one step. The two-argument overload
			// above is this one with no parts, and leaves any parts the template already
			// had alone - so editing a composed template's components does not silently
			// decompose it.
			virtual bool CreateTemplate(const std::string& name, const nlohmann::json& components,
							const nlohmann::json& parts, std::string& error);
			// Whether `part_template` can be a part of `name` - false when it is `name`
			// itself or reaches it through its own parts. Everything that adds a part
			// asks first, so the cycle is refused where it is authored rather than where
			// it would hang.
			virtual bool CanComposeTemplate(const std::string& name, const std::string& part_template) const;

			// Every entity name spawning `instance_name` from `template_name` produces,
			// in spawn order: the instance itself (or "<instance>_<n>" for each node of a
			// multi-part FBX template), followed by "<instance>__<part>" for each part,
			// recursively.
			//
			// The naming lives here because SpawnInstance is what applies it, and
			// anything that has to find an instance's entities afterwards - the editor's
			// removal, its undo, the bookkeeping it re-derives on load - would otherwise
			// reproduce the rule and drift from it.
			virtual std::vector<std::string> InstanceEntityNames(const std::string& instance_name,
							const std::string& template_name);
			// What separates an instance from the part of it a name belongs to. Doubled
			// so it cannot be confused with the "_<n>" a multi-part FBX template appends.
			static constexpr const char* PART_NAME_SEPARATOR = "__";
			// Destroys the template entity and forgets the definition. Entities already
			// spawned from it are untouched - they own their own components.
			virtual bool RemoveTemplate(const std::string& name);
			virtual bool IsAuthoredTemplate(const std::string& name) const;
			// The entity in the templates coordinator that stands for `name`: an
			// authored template's single entity, or the first renderable part of an
			// imported one. Anything reading a template's mesh, material or bounds goes
			// through this rather than through the coordinator's name lookup, because an
			// authored template's entity is deliberately not registered under the
			// template's own name (see TEMPLATE_ENTITY_PREFIX).
			virtual ECS::Entity GetTemplateEntity(const std::string& name);

			// The template's own base transform: the pose SpawnInstance *composes* into
			// every instance it spawns (position added, rotation multiplied, scale
			// multiplied). False - and the identity transform - for an unknown template.
			//
			// Anything that stores an instance's pose back into its record needs this,
			// because a record is in spawn space, not world space: writing the live
			// transform into it as-is makes the next spawn compose the base a second
			// time, which shrinks (and offsets, and re-rotates) the object on every
			// paste and every reload.
			virtual bool GetTemplateBaseTransform(const std::string& name, float3& position,
							float4& rotation, float3& scale);

			// Authored template entities are registered under this prefix. Template
			// entities of every kind share one coordinator, so without it a template
			// named after the object it represents ("troll") would collide with the FBX
			// node of that name and overwrite the mapping the imported template needs.
			// Naming a template after its subject is the natural thing to do, so the
			// prefix exists rather than the collision being forbidden.
			static constexpr const char* TEMPLATE_ENTITY_PREFIX = "__template_";
			// The component block of an authored template, or null for an unknown or
			// FBX-loaded one.
			virtual const nlohmann::json* GetTemplateComponents(const std::string& name) const;
			// Every registered template name, authored and FBX alike, sorted.
			virtual std::vector<std::string> ListTemplates() const;
			// Reads / writes the file form of an authored template, a .tpl holding
			// {"name": ..., "components": {...}}. `relative` resolves `file` against the
			// assets path exactly as LoadTemplate does; the name defaults to the file
			// stem when the JSON does not carry one.
			//
			// LoadTemplateFile reads and registers in one go, which is what an editor
			// importing a template wants. Load() splits the two (ReadTemplateFile, then
			// CreateTemplate later) because an authored template has to be created
			// *after* the level's materials and animation sets exist - see the comment
			// on the templates phase there.
			virtual bool ReadTemplateFile(const std::string& file, bool relative,
							std::string& name, nlohmann::json& components, nlohmann::json& parts,
							std::string& error);
			virtual bool LoadTemplateFile(const std::string& file, bool relative, std::string& error);
			virtual bool SaveTemplateFile(const std::string& name, const std::string& file,
							std::string& error);
			// The animation names a mesh asset offers, for an editor's animation picker.
			// Walks the skeletons the same way Mesh::SetAnimation resolves a name, so
			// everything listed here is something SetAnimation will accept.
			virtual std::vector<std::string> GetMeshAnimations(const std::string& mesh_name);
			// The clips one loaded animation set holds, sorted. An animation set is what
			// a skeleton is called once it is being chosen from rather than skinned with:
			// the clips an .fbx contributed, keyed by that file's stem.
			virtual std::vector<std::string> GetAnimationSetClips(const std::string& set_name) const;
			// The animation set holding `clip`, or "" when no loaded set does. This is
			// how naming a clip is enough to play it: the set that owns it is found and
			// attached rather than being named a second time (Mesh::FromJson, and every
			// editor surface that adds an animation to a template).
			virtual std::string FindAnimationSet(const std::string& clip) const;
			// Rebuilds the GPU vertex/BVH buffers from the current CPU-side mesh data.
			// Init() uploads them exactly once, so meshes added by LoadTemplate/LoadFBX
			// calls made after Init() (e.g. an editor importing objects into a running
			// session) otherwise reference GPU data that was never uploaded and render
			// as nothing. Call from the render thread, between frames.
			virtual void RefreshMeshBuffers();
			// Turns normal smoothing on or off for a mesh *asset*. Shared, like the
			// asset: every entity drawing `mesh` changes, which is the same scope the
			// ".NoSmooth" node name always had and the same scope Mesh's "skeletons"
			// key has. Two entities on one mesh asking for different values is
			// last-writer-wins, and both then serialize the value that won.
			//
			// The world vertex buffer is immutable and holds every mesh, so this only
			// rewrites the CPU side and marks it dirty; FlushMeshBuffers does the one
			// rebuild that puts it on screen. That split is what keeps a level whose
			// records flip a dozen meshes from rebuilding the whole buffer a dozen
			// times. False when nothing changed.
			virtual bool SetMeshSmooth(Core::MeshData* mesh, bool smooth);
			// Installs the level-of-detail chain of a mesh *asset*, by the names its
			// alternates are registered under - shared exactly like SetMeshSmooth
			// above, and for the same reason: a LOD is a description of the geometry,
			// so every entity drawing `mesh` follows it.
			//
			// `names` is finest first and must not include `mesh` itself, which is
			// always level 0. `distances` supplies the LOD_DISTANCE switch point of
			// each name, in the same order, and is ignored in LOD_AUTO. False when a
			// name did not resolve or an alternate was refused (MeshData::SetLods says
			// why on stdout); the levels that were usable are still installed, so a
			// level naming one bad LOD keeps the rest of the chain.
			virtual bool SetMeshLods(Core::MeshData* mesh, const std::vector<std::string>& names,
				const std::vector<float>& distances = {});
			// Builds a coarser stand-in for `source_mesh` by simplifying its geometry
			// (Core::SimplifyMesh) and registers it as a mesh asset of its own, named
			// `<source>_lod<n>`, which is what comes back in `out_name`. It is then a
			// mesh like any other: it can be named in a LOD chain, picked in an
			// editor's mesh list, and drawn by anything that draws a mesh - the point
			// being that a level of detail no longer has to be a second model somebody
			// exported by hand.
			//
			// `ratio` is the share of the source's vertices to aim for, in (0, 1).
			//
			// It is *stored*, not recomputed: the geometry is written to
			// GENERATED_MESH_DIR under the assets path and the level records the
			// name/source/ratio triple that produced it (see GeneratedMesh and the
			// "generated_meshes" section of Load), so the next load reads a file
			// instead of simplifying the model again. The file is a cache and is
			// treated as one - it is regenerated whenever it is missing, written by a
			// different build, or no longer matches the source mesh it claims to come
			// from.
			virtual bool GenerateMeshLod(const std::string& source_mesh, float ratio,
				std::string& out_name, std::string& error);
			// The generated meshes this world holds, in creation order - what a level
			// has to write out for a future load to find them again.
			const std::vector<GeneratedMesh>& GetGeneratedMeshes() const { return generated_meshes; }
			// Rebuilds the GPU buffers when a SetMeshSmooth since the last flush left
			// them stale, and does nothing otherwise - so it is safe (and meant) to be
			// called every frame, between frames. Before Init() it only clears the
			// flag: Init uploads the buffers for the first time and so picks the
			// changes up on its own.
			virtual void FlushMeshBuffers();
			// Spawns a new, persistable entity (or set of entities, for multi-part templates)
			// cloned from a named template, at the given base transform. Used both by the
			// "instances" section of Load() and by editor tooling that places objects at runtime.
			// `out_parts`, when given, receives every entity created - a multi-part
			// template produces one per FBX node, of which only the first is returned.
			// Callers applying per-instance data (component overrides) need all of them,
			// or the override would silently land on part 0 alone.
			virtual ECS::Entity SpawnInstance(const std::string& name, const std::string& template_name,
							const float3& position, const float4& rotation, const float3& scale,
							const std::string& material_name = "", const nlohmann::json* physics_json = nullptr,
							std::vector<ECS::Entity>* out_parts = nullptr);
			// Creates a new entity named `new_name` as a copy of the existing scene
			// entity `source_name`: Base flags, Transform, Bounds and the (shared)
			// mesh/material data are copied; Physics parameters are copied and a fresh
			// rigid body is created when the source has one. Only mesh entities
			// (Base+Transform+Bounds+Mesh) can be cloned; lights/cameras/sky return
			// INVALID_ENTITY_ID. Used by the "clones" section of Load() and by editor
			// copy/paste at runtime.
			virtual ECS::Entity CloneEntity(const std::string& new_name, const std::string& source_name);

			// Creates an entity carrying only the two components every entity must have:
			// Base (identity) and Transform (placement). No mesh, no bounds, no body -
			// so it draws nothing and belongs to no system until components are added
			// to it, which is exactly what the editor's Add/Entity is for.
			//
			// It is the one kind of entity that comes from no model, no template and no
			// other entity, so a level has to record its existence on its own: that is
			// the "created_entities" section of Load(), which is this call plus the
			// component blocks the entity was given. INVALID_ENTITY_ID when the name is
			// empty or already taken.
			virtual ECS::Entity CreateEmptyEntity(const std::string& name,
							const float3& position = { 0.0f, 0.0f, 0.0f },
							const float4& rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
							const float3& scale = { 1.0f, 1.0f, 1.0f });

			// Registers T with the ECS and, when T declares the serialization members
			// (see ECS/Serialization.h), with the component registry as well - so a
			// game's own components become level-authorable and editor-visible without
			// touching the engine or any central list. Components that don't declare
			// them are registered with the ECS exactly as before.
			template<typename T>
			void RegisterComponent(ECS::ComponentPolicy policy = ECS::ComponentPolicy::Full)
			{
				coordinator->RegisterComponent<T>();
				if constexpr (ECS::SerializableComponent<T>) {
					ECS::ComponentRegistry::Instance().Register<T>(policy);
				}
			}

			template<typename T>
			std::shared_ptr<T> RegisterSystem()
			{
				std::shared_ptr<T> system = coordinator->RegisterSystem<T>();
				systems_by_name[typeid(T).name()] = system;
				return system;
			}

			ECS::Coordinator* GetCoordinator() { 
				ECS::Coordinator* ret = nullptr;
				if (init) {
					ret = coordinator;
				}
				return ret; 
			}

			ECS::Coordinator* GetTemplatesCoordinator() {
				ECS::Coordinator* ret = nullptr;
				if (init) {
					ret = templates_coordinator;
				}
				return ret;
			}
			
			// Absolute path of the assets folder this world loaded from. Needed by
			// components that resolve file references during deserialization.
			const std::string& GetAssetsPath() const { return path; }

			// Stand-in assets for a Mesh or Material component created from nothing -
			// added in the editor, or a level record carrying an empty block.
			//
			// They exist so that adding a component is the exact inverse of removing
			// one: a component that can be taken away but never put back is a dead end
			// for whoever is authoring the scene. A newly added Mesh is a unit cube and
			// a newly added Material is plain white, so the entity is immediately
			// visible in the viewport and can then be pointed at a real asset.
			//
			// Created on first use and cached, under "__default_*" names that cannot
			// collide with anything an FBX or .mat file brings in. Null only if the
			// render device is not up yet.
			Core::MaterialData* GetDefaultMaterial();
			Core::MeshData* GetDefaultMesh();

			// The names the two above are registered under. A saved scene references
			// them like any other asset, so deserialization has to recognize them and
			// create the asset on demand rather than reporting it missing.
			static constexpr const char* DEFAULT_MATERIAL_NAME = "__default_material";
			static constexpr const char* DEFAULT_MESH_NAME = "__default_mesh";
			static constexpr const char* DEFAULT_SPLAT_CLOUD_NAME = "__default_splat_cloud";

			// The context handed to component ToJson/FromJson, bound to this world's
			// scene coordinator.
			ECS::SerializeContext MakeSerializeContext();

			// Applies one level record's component blocks to `e`:
			//
			//     "remove":     ["Physics", ...]        - applied first
			//     "components": {"Physics": {...}, ...} - add-or-update, applied second
			//
			// Removals run first so a record can replace a component wholesale, and so
			// that "strip what the template gave me, then add my own" reads in the order
			// it executes. Unknown component names are reported and skipped rather than
			// aborting the load - a level authored against a game that registers more
			// components than the current binary (the Scene Editor opening a Marbles
			// level) must still load everything it does understand.
			//
			// Shared by the "entities", "instances", "clones" and "created_entities"
			// phases of Load, and by editor tooling applying the same blocks at runtime.
			void ApplyComponents(ECS::Entity e, const nlohmann::json& entry);

			// Whether a level record explicitly removed `component` from this entity.
			// Anything that would otherwise add a component by default must consult
			// this, or the removal silently does not stick.
			bool IsComponentRemoved(ECS::Entity e, const std::string& component) const;

			Core::FlatMap<std::string, Core::MaterialData>& GetMaterials();

			// --- Material authoring -------------------------------------------------
			// The .mat files this world loaded: level-relative path -> texture root.
			const std::map<std::string, std::string>& GetMaterialFiles() const { return material_files; }
			// The .mat file `material_name` was loaded from, or "" when it came from an
			// FBX or from code and has never been assigned one.
			std::string GetMaterialOrigin(const std::string& material_name) const;
			// Assigns (or reassigns) a material to a .mat file. `mat_file` must already be
			// known to GetMaterialFiles(); returns false otherwise, since writing a
			// material into a file whose texture root is unknown would produce paths the
			// next load cannot resolve.
			bool SetMaterialOrigin(const std::string& material_name, const std::string& mat_file);
			// Creates an empty white material under `name`, registered against `mat_file`.
			// Returns null when the name is taken, the file is unknown, or the render
			// device is not up. The material is Init()ed and immediately assignable.
			Core::MaterialData* CreateMaterial(const std::string& name, const std::string& mat_file);
			// Retires a material: it stops being listed, stops being saved, and can no
			// longer be assigned - but its MaterialData stays alive (see the comment on
			// the implementation for why erasing it would dangle an *unrelated*
			// material). Callers are responsible for having already repointed every
			// entity that used it; this does not touch entities.
			bool RemoveMaterial(const std::string& name);
			bool IsMaterialRemoved(const std::string& name) const;
			// Un-retires a material and reassigns it to a file. The undo of
			// RemoveMaterial, and the reason removal keeps the data alive.
			bool RestoreMaterial(const std::string& name, const std::string& mat_file);
			// Rewrites `mat_file` from the in-memory state of every material assigned to
			// it, preserving the file's "root" declaration and the keys Load() ignores
			// (see MaterialData::source_json). Returns false if the file is unknown or
			// cannot be written.
			bool SaveMaterialFile(const std::string& mat_file);
			// Repoints an entity's Material component at the named material and
			// re-registers it with the render system, which keys its draw trees by
			// MaterialData pointer - assigning the pointer alone leaves the entity drawing
			// with the old material until something forces a signature change.
			bool SetEntityMaterial(ECS::Entity e, const std::string& material_name);
			// Rebinds a material's shader stages and re-registers every entity using it
			// with the render system. Returns false (changing nothing) when the material
			// is unknown or any name does not load as the stage it was given for.
			bool SetMaterialShaders(const std::string& material_name,
									const Core::MaterialShaderNames& names);

			// --- Multi-materials ----------------------------------------------------
			//
			// A multi-material is a named stack of material layers - the terrain painted
			// with dirt, grass and rock through one mask image, with snow on whatever
			// faces up. It is an asset like a material: stored in a .mat file's
			// "multi_materials" array, shared by every level that references the file,
			// and saved by SaveMaterialFile rather than with the level.
			//
			// A *material* carries one (MaterialData::multi_material_name), and that is
			// what makes a surface use it. It is deliberately not per entity: the render
			// trees are keyed by material, so all the entities in one bucket are drawn
			// with a single set of layer constants and a per-entity stack could only ever
			// be honoured for whichever of them the bucket happened to hold first.
			//
			// Names live in their own namespace, so a multi-material may share a name
			// with the material that uses it.
			std::vector<std::string> ListMultiMaterials() const;
			Core::MultiMaterialData* GetMultiMaterial(const std::string& name);
			// Inserts or replaces `name` and rebuilds its GPU arrays. Does not assign it
			// to a file (CreateMultiMaterial does) - this is the "apply an edit" entry
			// point, used by the editor's undo as much as by loading.
			void SetMultiMaterial(const std::string& name, const Core::MultiMaterialData& data);
			void LoadMultiMaterial(const std::string& name, const nlohmann::json& multi_material_info);
			// Creates an empty stack under `name`, registered against `mat_file`. Null
			// when the name is taken or the file is unknown, exactly like CreateMaterial.
			Core::MultiMaterialData* CreateMultiMaterial(const std::string& name, const std::string& mat_file);
			// Retires a stack and detaches it from every material using it. Like
			// RemoveMaterial the data is kept, so RestoreMultiMaterial can undo this.
			bool RemoveMultiMaterial(const std::string& name);
			bool RestoreMultiMaterial(const std::string& name, const std::string& mat_file);
			bool IsMultiMaterialRemoved(const std::string& name) const;
			std::string GetMultiMaterialOrigin(const std::string& name) const;
			// Attaches a stack to a material, or detaches it when `multi_material_name`
			// is empty. Re-registers the material's entities with the render system,
			// because a stack changes which shader path they take.
			bool SetMaterialMultiMaterial(const std::string& material_name,
										  const std::string& multi_material_name);
			// Re-resolves every material's stack pointer and rebuilds every stack from
			// its layers. Needed after loading (a material can name a stack the file
			// declares below it), after a material's textures change (the layer flags
			// record which maps exist) and after any layer edit.
			void ResolveMultiMaterials();

			Core::FlatMap<std::string, Core::MeshData>& GetMeshes();
			// The single vertex/index buffer every mesh this world loaded lives in.
			// A MeshData does not own GPU buffers of its own - it holds an offset pair
			// into this one - so anything drawing a mesh outside the RenderSystem (an
			// editor preview pass, a debug overlay) has to bind this and then draw with
			// the mesh's indexOffset/vertexOffset, exactly as RenderSystem does.
			Core::VertexBuffer<Core::Vertex>* GetVertexBuffer() { return vertex_buffer; }
			Core::FlatMap<std::string, Core::SplatCloudData>& GetSplatClouds();
			// Imports a .ply Gaussian splat cloud and registers it under `name`, uploading
			// it to the GPU. Returns null on a failed read or an unusable file; the reason
			// is logged. Re-registering an existing name returns the one already there
			// rather than re-reading the file.
			Core::SplatCloudData* LoadSplatCloud(const std::string& file, const std::string& name);
			// The stand-in cloud, created on demand: a small sphere of splats, so a
			// SplatCloud component added with no asset picked is visible and swappable
			// instead of being an invisible entity that looks like a bug.
			Core::SplatCloudData* GetDefaultSplatCloud();
			Core::FlatMap<std::string, Core::ShapeData>& GetShapes();
			Core::FlatMap<std::string, std::shared_ptr<Core::Skeleton>>& GetSkeletons();
			reactphysics3d::PhysicsWorld* GetPhysicsWorld();

			// The collision mesh Init() gave (or would give) this entity's Physics
			// component: its own FBX mesh shape, or its clone source's. Null when the
			// entity has none, which is also the correct answer for dynamic bodies -
			// they always get a primitive capsule/box/sphere instead. Callers that
			// rebuild a collider (Physics::UpdateShape) must pass exactly this.
			Core::ShapeData* GetEntityShape(const std::string& entity_name);

			// Keeps GetEntityShape working across a rename: collision meshes are keyed
			// by the name the entity was loaded under, so an editor that renames an
			// entity must point the new name at the same shape. No-op when the old
			// name has no shape of its own to inherit.
			void AliasEntityShape(const std::string& old_name, const std::string& new_name);

			template<class T>
			std::shared_ptr<T>  GetSystem() {
				std::shared_ptr<T> system;
				auto it = systems_by_name.find(typeid(T).name());
				if (it != systems_by_name.end()) {
					system = std::dynamic_pointer_cast<T>(it->second);
				}
				return system;
			}
		};
	}
}