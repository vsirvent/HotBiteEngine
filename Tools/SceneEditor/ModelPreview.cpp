#include "ModelPreview.h"

#include "PreviewPass.h"

#include <Components/Base.h>
#include <Core/DXCore.h>
#include <Core/Material.h>
#include <Core/Mesh.h>
#include <Core/SimpleShader.h>
#include <Core/Vertex.h>

#include <DirectXMath.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::ECS;
using namespace DirectX;

namespace HotBiteEditor {
	namespace ModelPreview {
		namespace {

			//Vertical field of view. Narrow enough that a model framed to fill the
			//viewport does not read as fish-eyed at the close distances a small
			//preview uses.
			constexpr float FOV_Y = 0.25f * XM_PI;
			//How far the pitch may travel before the up vector degenerates.
			constexpr float MAX_PITCH = 1.45f;

			//One renderable part of the template, resolved fresh every frame.
			struct Part {
				Entity entity = INVALID_ENTITY_ID;
				Core::MeshData* mesh = nullptr;
				Core::MaterialData* material = nullptr;
				Components::Mesh* source = nullptr;   //the template's own component
				Components::Mesh* animator = nullptr; //the preview's own copy of it
				matrix world;                         //composed from the part's Transform
				//The part's extents in mesh space, which is what the camera frames.
				//Not simply MeshData's own: see MeasureSkinnedBounds.
				float3 local_min{};
				float3 local_max{};
			};

			//The preview's copy of a part's animation state. The template entity's own
			//Components::Mesh is never updated: it is what SpawnInstance clones, and
			//advancing its clock here would leave every instance spawned afterwards
			//starting mid-stride.
			struct Animator {
				std::unique_ptr<Components::Mesh> mesh;
				Core::MeshData* data = nullptr; //what it was built for
				std::string animation;
				//Measured once, at the pose the animation starts in - see
				//MeasureSkinnedBounds for why it is measured at all, and why once.
				bool bounds_valid = false;
				float3 bounds_min{};
				float3 bounds_max{};
			};

			struct Preview {
				PreviewPass::Target target;
				std::map<Entity, Animator> animators;
				//Which editor frame this preview was last drawn in, so the target of a
				//template nobody is looking at can be given back - see ReleaseIdle.
				int last_drawn_frame = -1;
			};

			struct State {
				Core::SimpleVertexShader* vs = nullptr;
				Core::SimplePixelShader* ps = nullptr;
				bool shaders_missing = false;
				std::map<std::string, Preview> previews;

				//The preview's own animation clock, advanced once per editor frame
				//while something is playing. Separate from the scene's so pausing a
				//preview does not stop the level, and so a paused preview holds its
				//pose instead of jumping when it resumes.
				uint64_t anim_nsec = 0;
				uint64_t anim_elapsed_nsec = 0;
				std::chrono::steady_clock::time_point last_tick{};
				int last_tick_frame = -1;
			};

			State& Get_() {
				static State state;
				return state;
			}

			bool EnsureShaders() {
				State& s = Get_();
				if (s.vs != nullptr && s.ps != nullptr) {
					return true;
				}
				if (s.shaders_missing) {
					return false;
				}
				s.vs = Core::ShaderFactory::Get()->GetShader<Core::SimpleVertexShader>("ModelPreviewVS.cso");
				//Shared with the material thumbnails on purpose - see ModelPreviewVS.hlsl.
				s.ps = Core::ShaderFactory::Get()->GetShader<Core::SimplePixelShader>("MaterialPreviewPS.cso");
				if (s.vs == nullptr || s.ps == nullptr) {
					//Remember the failure, or every frame retries a file load that is
					//not going to start working.
					printf("ModelPreview: preview shaders not available, model preview disabled\n");
					s.shaders_missing = true;
					return false;
				}
				return true;
			}

			//Advances the shared animation clock, at most once per editor frame however
			//many previews are drawn.
			void TickClock(bool playing) {
				State& s = Get_();
				const int frame = ImGui::GetFrameCount();
				if (s.last_tick_frame == frame) {
					return;
				}
				const auto now = std::chrono::steady_clock::now();
				uint64_t elapsed = 0;
				if (s.last_tick_frame >= 0) {
					elapsed = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
						now - s.last_tick).count();
					//A preview that was closed, or an editor that stalled on a level
					//load, must not fast-forward the animation by the whole gap.
					elapsed = (std::min<uint64_t>)(elapsed, 100000000ull);
				}
				s.last_tick = now;
				s.last_tick_frame = frame;
				if (playing) {
					s.anim_elapsed_nsec = elapsed;
					s.anim_nsec += elapsed;
				}
				else {
					s.anim_elapsed_nsec = 0;
				}
			}

			//The template's parts, in the form the draw needs. Read live rather than
			//cached: a template edit rewrites these components in place, and a preview
			//that had to be told about every edit would sooner or later miss one.
			std::vector<Part> CollectParts(EditorState& state, const std::string& template_name) {
				std::vector<Part> parts;
				Coordinator* tc = state.world->GetTemplatesCoordinator();
				if (tc == nullptr) {
					return parts;
				}
				for (Entity e : state.world->GetTemplateEntities(template_name)) {
					if (!tc->ContainsComponent<Components::Mesh>(e) ||
						!tc->ContainsComponent<Transform>(e)) {
						continue;
					}
					if (tc->ContainsComponent<Base>(e) && !tc->GetComponent<Base>(e).visible) {
						continue;
					}
					Components::Mesh& mesh = tc->GetComponent<Components::Mesh>(e);
					Core::MeshData* data = mesh.GetData();
					if (data == nullptr || data->indexCount == 0) {
						continue;
					}

					Part part;
					part.entity = e;
					part.mesh = data;
					part.source = &mesh;
					if (tc->ContainsComponent<Components::Material>(e)) {
						part.material = tc->GetComponent<Components::Material>(e).data;
					}
					if (part.material == nullptr) {
						part.material = state.world->GetDefaultMaterial();
					}
					if (part.material == nullptr) {
						continue;
					}

					//Composed here rather than read from Transform::world_matrix: the
					//templates coordinator runs no systems, so nothing ever computes
					//that matrix and it is still all zeros.
					const Transform& t = tc->GetConstComponent<Transform>(e);
					part.world = XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z) *
						XMMatrixRotationQuaternion(XMLoadFloat4(&t.rotation)) *
						XMMatrixTranslation(t.position.x, t.position.y, t.position.z);
					parts.push_back(part);
				}
				return parts;
			}

			//The extents of a skinned mesh in the pose it is actually drawn in.
			//
			//MeshData::minDimensions/maxDimensions measure the vertex positions as they
			//sit in the buffer, which for a skinned mesh is the space *before* the
			//skinning matrices are applied - and for a rig whose bind pose is authored
			//somewhere other than where the animation puts it, that box can be several
			//times the size of the model on screen. Framing on it is what made the demo
			//troll appear as a speck in the middle of an empty viewport.
			//
			//So the same blend the vertex shader does is run once on the CPU. Once, not
			//per frame: the cost is a pass over every vertex, and framing that tracked
			//the current pose would make the model breathe in and out of the viewport as
			//the animation played, which is worse than a box that is slightly loose at
			//the extremes of a stride.
			void MeasureSkinnedBounds(Animator& animator, Core::MeshData* mesh) {
				animator.bounds_valid = true;
				animator.bounds_min = mesh->minDimensions;
				animator.bounds_max = mesh->maxDimensions;

				const std::vector<Core::JointGpuData>& joints = animator.mesh->joint_gpu_data;
				//No animation playing means no joints are uploaded and the shader draws
				//the raw positions, which is exactly what MeshData already measured.
				if (animator.mesh->GetCurrentAnimationId() < 0 || joints.empty() ||
					mesh->vertices.empty()) {
					return;
				}
				const int njoints = (int)joints.size();

				XMVECTOR min_v = XMVectorReplicate(FLT_MAX);
				XMVECTOR max_v = XMVectorReplicate(-FLT_MAX);
				bool any = false;
				for (const Core::Vertex& vertex : mesh->vertices) {
					const float* weights = &vertex.Weights.x;
					XMMATRIX skin{};
					bool skinned = false;
					for (int i = 0; i < 4; ++i) {
						if (vertex.Boneids[i] < 0 || weights[i] <= 0.0f ||
							vertex.Boneids[i] >= njoints) {
							continue;
						}
						//joint_gpu_data holds the matrices already transposed for upload,
						//so the accumulated blend has to be transposed back before it can
						//be used as a row-vector transform here.
						skin += XMLoadFloat4x4(&joints[vertex.Boneids[i]].skinning_matrix) *
							weights[i];
						skinned = true;
					}
					XMVECTOR position = XMLoadFloat3(&vertex.Position);
					if (skinned) {
						position = XMVector3Transform(position, XMMatrixTranspose(skin));
					}
					min_v = XMVectorMin(min_v, position);
					max_v = XMVectorMax(max_v, position);
					any = true;
				}
				if (any) {
					XMStoreFloat3(&animator.bounds_min, min_v);
					XMStoreFloat3(&animator.bounds_max, max_v);
				}
			}

			//World-space bounds of every part, which is what the camera frames. Uses the
			//measured mesh extents rather than the Bounds component: an authored
			//template's Bounds can be overridden to something that is not the
			//silhouette, and framing on that would put the model off-centre.
			bool MeasureParts(const std::vector<Part>& parts, XMVECTOR& center, float& radius) {
				XMVECTOR min_v = XMVectorReplicate(FLT_MAX);
				XMVECTOR max_v = XMVectorReplicate(-FLT_MAX);
				bool any = false;
				for (const Part& part : parts) {
					const float3& lo = part.local_min;
					const float3& hi = part.local_max;
					for (int corner = 0; corner < 8; ++corner) {
						const XMVECTOR local = XMVectorSet(
							(corner & 1) ? hi.x : lo.x,
							(corner & 2) ? hi.y : lo.y,
							(corner & 4) ? hi.z : lo.z, 1.0f);
						const XMVECTOR world = XMVector3Transform(local, part.world);
						min_v = XMVectorMin(min_v, world);
						max_v = XMVectorMax(max_v, world);
						any = true;
					}
				}
				if (!any) {
					return false;
				}
				center = XMVectorScale(XMVectorAdd(min_v, max_v), 0.5f);
				radius = 0.5f * XMVectorGetX(XMVector3Length(XMVectorSubtract(max_v, min_v)));
				//A degenerate mesh (a single point, or one the loader produced empty)
				//would put the camera exactly on the model and divide by zero below.
				radius = (std::max)(radius, 1e-4f);
				return true;
			}

			//The preview's animation state for one part, built or rebuilt when the
			//template's mesh or animation changed under it. `override_animation`, when
			//given, is a logical name from the template's own library being auditioned
			//in place of what the template says it plays.
			Animator& EnsureAnimator(Preview& preview, const Part& part,
				const std::string& override_animation) {
				const std::string animation = override_animation.empty()
					? part.source->GetCurrentAnimationName() : override_animation;
				Animator& animator = preview.animators[part.entity];
				if (animator.mesh != nullptr && animator.data == part.mesh &&
					animator.animation == animation) {
					return animator;
				}
				animator.mesh = std::make_unique<Components::Mesh>();
				animator.data = part.mesh;
				animator.animation = animation;
				animator.bounds_valid = false;
				//The template's animation library comes along, because the names in it
				//are the only ones that mean anything here: a template plays "walk", and
				//a bare Mesh would search the skeletons for a clip by that name and find
				//nothing.
				animator.mesh->clips = part.source->clips;
				animator.mesh->SetData(part.mesh);
				if (animation.empty()) {
					//"No animation" is a real choice a template can make, distinct from
					//"never picked one" - which is what SetData just left behind (the
					//first animation of the first skeleton, playing).
					animator.mesh->StopAnimation();
				}
				else {
					animator.mesh->SetAnimation(animation, true, false, 0.0f);
				}
				//No coordinator: the preview must not emit animation-end or frame
				//events into the templates coordinator, where nothing is listening for
				//them and every listener that did would fire once per preview frame.
				return animator;
			}

			//Advances every part's animation and resolves the extents the camera frames.
			//Runs before the camera is placed, because with a skinned mesh the pose is
			//what decides where the model is.
			void PrepareParts(Preview& preview, std::vector<Part>& parts,
				const std::string& override_animation) {
				State& s = Get_();
				for (Part& part : parts) {
					Animator& animator = EnsureAnimator(preview, part, override_animation);
					animator.mesh->Update((int64_t)s.anim_elapsed_nsec, (int64_t)s.anim_nsec);
					if (!animator.bounds_valid) {
						MeasureSkinnedBounds(animator, part.mesh);
					}
					part.animator = animator.mesh.get();
					part.local_min = animator.bounds_min;
					part.local_max = animator.bounds_max;
				}
			}

			void RenderModel(const Preview& preview, const std::vector<Part>& parts,
				const View& view, EditorState& state) {
				State& s = Get_();
				Core::DXCore* dx = Core::DXCore::Get();
				ID3D11DeviceContext* context = dx->context;

				XMVECTOR center;
				float radius = 1.0f;
				if (!MeasureParts(parts, center, radius)) {
					return;
				}

				//Distance that fits the bounding sphere in the narrower of the two
				//field-of-view angles, so a wide viewport frames the model the same way
				//a tall one does.
				const float aspect = (float)preview.target.width / (float)preview.target.height;
				const float fov_x = 2.0f * atanf(tanf(FOV_Y * 0.5f) * aspect);
				const float fit = radius / sinf((std::min)(FOV_Y, fov_x) * 0.5f);
				const float distance = fit * view.zoom;

				const XMVECTOR direction = XMVectorSet(
					cosf(view.pitch) * sinf(view.yaw),
					sinf(view.pitch),
					cosf(view.pitch) * cosf(view.yaw), 0.0f);
				const XMVECTOR eye = XMVectorAdd(center, XMVectorScale(direction, distance));

				const matrix view_m = XMMatrixLookAtLH(eye, center, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
				//Depth range straddles the model rather than being fixed: templates are
				//authored at wildly different scales (0.025 for the demo troll, tens of
				//units for terrain pieces) and one fixed near plane either clips the
				//small ones away or z-fights across the large ones.
				const float near_z = (std::max)(1e-4f, (distance - radius) * 0.5f);
				const float far_z = distance + radius * 4.0f;
				const matrix projection_m = XMMatrixPerspectiveFovLH(FOV_Y, aspect, near_z, far_z);

				float4x4 view_t, projection_t;
				XMStoreFloat4x4(&view_t, XMMatrixTranspose(view_m));
				XMStoreFloat4x4(&projection_t, XMMatrixTranspose(projection_m));
				float3 camera_position, focus_position;
				XMStoreFloat3(&camera_position, eye);
				XMStoreFloat3(&focus_position, center);
				//Rebuilt every frame from the orbit, which is what keeps the model lit
				//from the front whichever way it has been turned - see PreviewPass.h.
				const PreviewPass::LightRig rig =
					PreviewPass::MakeLightRig(camera_position, focus_position);

				Core::VertexBuffer<Core::Vertex>* vb = state.world->GetVertexBuffer();
				if (vb == nullptr) {
					return;
				}
				vb->SetBuffers();
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->OMSetBlendState(dx->no_blend, nullptr, 0xFFFFFFFF);
				context->OMSetDepthStencilState(dx->normal_depth, 0);
				context->RSSetState(dx->drawing_rasterizer);

				for (const Part& part : parts) {
					float4x4 world_t, world_inv_t;
					//Same convention the engine stores on Transform: the shader gets the
					//transposed world matrix, and the transposed inverse for normals.
					XMStoreFloat4x4(&world_t, XMMatrixTranspose(part.world));
					XMStoreFloat4x4(&world_inv_t,
						XMMatrixTranspose(XMMatrixInverse(nullptr, part.world)));

					s.vs->SetShader();
					s.vs->SetMatrix4x4("world", world_t);
					s.vs->SetMatrix4x4("worldInv", world_inv_t);
					s.vs->SetMatrix4x4("view", view_t);
					s.vs->SetMatrix4x4("projection", projection_t);
					//Uploads the skinning matrices and njoints, exactly as the main
					//render path does for a scene entity.
					part.animator->Prepare(s.vs);
					s.vs->CopyAllBufferData();

					s.ps->SetShader();
					s.ps->SetData("material", &part.material->props, sizeof(Core::MaterialProps));
					s.ps->SetFloat3("cameraPosition", camera_position);
					PreviewPass::BindLightRig(s.ps, rig);
					s.ps->SetSamplerState("basicSampler", dx->basic_sampler);
					s.ps->SetShaderResourceView("diffuseTexture", part.material->diffuse);
					s.ps->SetShaderResourceView("normalTexture", part.material->normal);
					s.ps->SetShaderResourceView("specularTexture", part.material->spec);
					s.ps->SetShaderResourceView("aoTexture", part.material->ao);
					s.ps->SetShaderResourceView("armTexture", part.material->arm);
					s.ps->SetShaderResourceView("emissionTexture", part.material->emission);
					s.ps->SetShaderResourceView("opacityTexture", part.material->opacity);
					s.ps->CopyAllBufferData();

					context->DrawIndexed((UINT)part.mesh->indexCount,
						(UINT)part.mesh->indexOffset, (INT)part.mesh->vertexOffset);
				}
			}

			//Gives back the render target of every preview that was not drawn this
			//frame. The panel shows one template at a time and a project can hold
			//hundreds, so without this a browse through the list leaves one viewport-
			//sized colour+depth pair per template alive for the session. The animators
			//are deliberately kept: they are small, and holding them is what lets a
			//template you come back to resume its animation instead of snapping to the
			//first frame.
			void ReleaseIdle(int frame) {
				for (auto& entry : Get_().previews) {
					if (entry.second.last_drawn_frame != frame &&
						entry.second.target.Valid()) {
						entry.second.target.Release();
					}
				}
			}

			//Left-drag orbits, wheel zooms, double-click reframes. Returns whether the
			//template is currently animating, which is what decides if the clock runs.
			void HandleInput(View& view, bool hovered, bool active) {
				if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
					const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
					view.yaw -= drag.x * 0.01f;
					view.pitch = std::clamp(view.pitch + drag.y * 0.01f, -MAX_PITCH, MAX_PITCH);
					ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
				}
				if (hovered) {
					const float wheel = ImGui::GetIO().MouseWheel;
					if (wheel != 0.0f) {
						view.zoom = std::clamp(view.zoom * (1.0f - wheel * 0.1f), 0.2f, 5.0f);
					}
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						const bool play = view.play;
						view = View{};
						view.play = play;
					}
				}
			}
		}

		bool Draw(EditorState& state, const std::string& template_name, View& view,
			const ImVec2& size) {
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			//An invisible button rather than an Image, because an Image is not an
			//interactive item and the orbit drag needs one that owns the mouse.
			ImGui::InvisibleButton("##model_preview", size,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool hovered = ImGui::IsItemHovered();
			const bool active = ImGui::IsItemActive();
			const ImVec2 corner(origin.x + size.x, origin.y + size.y);
			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			//Whatever happens below, the widget's footprint is drawn - an empty frame
			//says "there is a preview here and it has nothing to show" far better than
			//the panel silently collapsing by the height of the viewport.
			auto placeholder = [&](const char* message) {
				draw_list->AddRectFilled(origin, corner, IM_COL32(28, 29, 33, 255), 4.0f);
				draw_list->AddRect(origin, corner, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
				const ImVec2 text = ImGui::CalcTextSize(message);
				draw_list->AddText(ImVec2(origin.x + (size.x - text.x) * 0.5f,
					origin.y + (size.y - text.y) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), message);
				return false;
			};

			if (state.world == nullptr || size.x < 1.0f || size.y < 1.0f) {
				return placeholder("No preview");
			}
			Core::DXCore* dx = Core::DXCore::Get();
			if (dx == nullptr || dx->device == nullptr || dx->context == nullptr) {
				return placeholder("No preview");
			}
			if (!EnsureShaders()) {
				return placeholder("Preview shaders unavailable");
			}

			std::vector<Part> parts = CollectParts(state, template_name);
			if (parts.empty()) {
				return placeholder("Nothing to preview");
			}

			HandleInput(view, hovered, active);

			State& s = Get_();
			Preview& preview = s.previews[template_name];
			if (!preview.target.Ensure((int)size.x, (int)size.y)) {
				return placeholder("Preview target unavailable");
			}
			preview.last_drawn_frame = ImGui::GetFrameCount();
			ReleaseIdle(preview.last_drawn_frame);

			//Whether anything is actually animating: a template with no animation
			//costs the clock nothing, and the pose it shows is the same every frame.
			bool animating = !view.clip_override.empty();
			for (const Part& part : parts) {
				if (!part.source->GetCurrentAnimationName().empty()) {
					animating = true;
					break;
				}
			}
			TickClock(view.play && animating);
			PrepareParts(preview, parts, view.clip_override);

			{
				//Everything the draw touches is put back when this goes out of scope -
				//this runs in the middle of the editor's own frame. See PreviewPass.h.
				PreviewPass::ScopedState scoped;
				const float clear[4] = { 0.11f, 0.115f, 0.13f, 1.0f };
				preview.target.Bind(clear);
				RenderModel(preview, parts, view, state);
			}

			draw_list->AddImage(preview.target.Handle(), origin, corner);
			draw_list->AddRect(origin, corner, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
			return true;
		}

		void Shutdown() {
			State& s = Get_();
			for (auto& entry : s.previews) {
				entry.second.target.Release();
			}
			s.previews.clear();
			//The shaders belong to ShaderFactory, which owns their lifetime; only drop
			//our references so a later Shutdown/Draw cycle re-fetches them.
			s.vs = nullptr;
			s.ps = nullptr;
			s.shaders_missing = false;
			s.anim_nsec = 0;
			s.anim_elapsed_nsec = 0;
			s.last_tick_frame = -1;
		}
	}
}
