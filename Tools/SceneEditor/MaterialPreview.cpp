#include "MaterialPreview.h"

#include "PreviewPass.h"

#include <Core/DXCore.h>
#include <Core/SimpleShader.h>
#include <Core/Vertex.h>

#include <DirectXMath.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace HotBite::Engine;
using namespace DirectX;

namespace HotBiteEditor {
	namespace MaterialPreview {
		namespace {

			//Sphere tessellation. Enough segments that the silhouette reads as round at
			//thumbnail sizes without the mesh being worth caring about.
			constexpr int SPHERE_SLICES = 32;
			constexpr int SPHERE_STACKS = 24;

			//The material half of what the preview pixel shader consumes, laid out to
			//match the head of its `externalData` cbuffer. Core::MaterialProps is copied
			//in wholesale (the shader binds it as the same MaterialColor layout the main
			//render path uses), followed by the camera position the specular term needs.
			//The rest of that cbuffer is the light rig, which comes from the camera
			//rather than from the material and is bound by PreviewPass::BindLightRig.
			struct PreviewConstants {
				Core::MaterialProps props;
				float3 camera_position;
				float padding = 0.0f;
			};

			struct Thumbnail {
				PreviewPass::Target target;
				bool dirty = true;
			};

			struct State {
				ID3D11Buffer* vertex_buffer = nullptr;
				ID3D11Buffer* index_buffer = nullptr;
				UINT index_count = 0;
				Core::SimpleVertexShader* vs = nullptr;
				Core::SimplePixelShader* ps = nullptr;
				bool shaders_missing = false;
				//Keyed by material name AND size: the panel shows the same material at
				//two sizes in one frame (a list thumbnail and the large detail preview).
				//With a name-only key those two would fight over one render target,
				//re-creating it twice per frame - and, because ImGui only reads the
				//texture when the frame is submitted at the end, the one drawn first
				//would be reading an SRV that had already been released.
				std::map<std::pair<std::string, int>, Thumbnail> thumbnails;
			};

			State& Get_() {
				static State state;
				return state;
			}

			//A UV sphere in Core::Vertex form, so the preview vertex shader can declare
			//the engine's standard VertexShaderInput and let SimpleShader reflect the
			//input layout exactly as it does for every other shader.
			bool EnsureSphere(ID3D11Device* device) {
				State& s = Get_();
				if (s.vertex_buffer != nullptr) {
					return true;
				}
				std::vector<Core::Vertex> vertices;
				std::vector<uint32_t> indices;
				vertices.reserve((SPHERE_STACKS + 1) * (SPHERE_SLICES + 1));
				for (int stack = 0; stack <= SPHERE_STACKS; ++stack) {
					const float v = (float)stack / (float)SPHERE_STACKS;
					const float phi = v * XM_PI;
					for (int slice = 0; slice <= SPHERE_SLICES; ++slice) {
						const float u = (float)slice / (float)SPHERE_SLICES;
						const float theta = u * XM_2PI;

						Core::Vertex vertex;
						const float3 normal = {
							sinf(phi) * cosf(theta),
							cosf(phi),
							sinf(phi) * sinf(theta)
						};
						vertex.Position = normal; //unit sphere: position == normal
						vertex.Normal = normal;
						vertex.UV = { u, v };
						vertex.MeshUV = vertex.UV;
						//Tangent runs along increasing theta, bitangent completes the frame.
						vertex.Tangent = { -sinf(theta), 0.0f, cosf(theta) };
						XMVECTOR n = XMLoadFloat3(&vertex.Normal);
						XMVECTOR t = XMLoadFloat3(&vertex.Tangent);
						XMStoreFloat3(&vertex.Bitangent, XMVector3Normalize(XMVector3Cross(n, t)));
						vertices.push_back(vertex);
					}
				}
				const int stride = SPHERE_SLICES + 1;
				for (int stack = 0; stack < SPHERE_STACKS; ++stack) {
					for (int slice = 0; slice < SPHERE_SLICES; ++slice) {
						const uint32_t a = (uint32_t)(stack * stride + slice);
						const uint32_t b = (uint32_t)(a + stride);
						indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
					}
				}

				D3D11_BUFFER_DESC vb_desc{};
				vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
				vb_desc.ByteWidth = (UINT)(sizeof(Core::Vertex) * vertices.size());
				vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				D3D11_SUBRESOURCE_DATA vb_data{};
				vb_data.pSysMem = vertices.data();
				if (FAILED(device->CreateBuffer(&vb_desc, &vb_data, &s.vertex_buffer))) {
					return false;
				}

				D3D11_BUFFER_DESC ib_desc{};
				ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
				ib_desc.ByteWidth = (UINT)(sizeof(uint32_t) * indices.size());
				ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
				D3D11_SUBRESOURCE_DATA ib_data{};
				ib_data.pSysMem = indices.data();
				if (FAILED(device->CreateBuffer(&ib_desc, &ib_data, &s.index_buffer))) {
					RELEASE_PTR(s.vertex_buffer);
					return false;
				}
				s.index_count = (UINT)indices.size();
				return true;
			}

			bool EnsureShaders() {
				State& s = Get_();
				if (s.vs != nullptr && s.ps != nullptr) {
					return true;
				}
				if (s.shaders_missing) {
					return false;
				}
				s.vs = Core::ShaderFactory::Get()->GetShader<Core::SimpleVertexShader>("MaterialPreviewVS.cso");
				s.ps = Core::ShaderFactory::Get()->GetShader<Core::SimplePixelShader>("MaterialPreviewPS.cso");
				if (s.vs == nullptr || s.ps == nullptr) {
					//Remember the failure: without this the panel would retry the (failing)
					//file load once per material per frame.
					printf("MaterialPreview: preview shaders not available, thumbnails disabled\n");
					s.shaders_missing = true;
					return false;
				}
				return true;
			}

			void RenderThumbnail(Core::MaterialData* material, Thumbnail& t) {
				State& s = Get_();
				Core::DXCore* dx = Core::DXCore::Get();
				ID3D11DeviceContext* context = dx->context;

				//This runs while a panel is building its UI, in the middle of the editor's
				//own frame - every pipeline binding it touches is saved and put back, so a
				//thumbnail redraw can never perturb the scene or ImGui draw that follow.
				PreviewPass::ScopedState scoped;
				//Transparent clear: the panel composites the sphere over its own
				//background, so opacity is visible as actual transparency.
				const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				t.target.Bind(clear);

				//A fixed three-quarter view of a unit sphere, framed to just fill the tile.
				const float3 camera_position = { 0.0f, 0.9f, -2.6f };
				const XMMATRIX world = XMMatrixIdentity();
				const XMMATRIX view = XMMatrixLookAtLH(
					XMVectorSet(camera_position.x, camera_position.y, camera_position.z, 1.0f),
					XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
				const XMMATRIX projection = XMMatrixPerspectiveFovLH(0.30f * XM_PI, 1.0f, 0.1f, 100.0f);

				//Shaders take matrices transposed, the same convention CameraSystem uses.
				float4x4 world_t, view_t, projection_t;
				XMStoreFloat4x4(&world_t, XMMatrixTranspose(world));
				XMStoreFloat4x4(&view_t, XMMatrixTranspose(view));
				XMStoreFloat4x4(&projection_t, XMMatrixTranspose(projection));

				s.vs->SetShader();
				s.vs->SetMatrix4x4("world", world_t);
				s.vs->SetMatrix4x4("view", view_t);
				s.vs->SetMatrix4x4("projection", projection_t);
				s.vs->CopyAllBufferData();

				PreviewConstants constants;
				constants.props = material->props;
				constants.camera_position = camera_position;

				//The same rig the model viewport builds, from this thumbnail's fixed
				//camera - which is what makes a material read the same in its swatch as
				//it does on a model (PreviewPass.h).
				const PreviewPass::LightRig rig =
					PreviewPass::MakeLightRig(camera_position, float3(0.0f, 0.0f, 0.0f));

				s.ps->SetShader();
				s.ps->SetData("material", &constants.props, sizeof(Core::MaterialProps));
				s.ps->SetFloat3("cameraPosition", constants.camera_position);
				PreviewPass::BindLightRig(s.ps, rig);
				s.ps->SetSamplerState("basicSampler", dx->basic_sampler);
				s.ps->SetShaderResourceView("diffuseTexture", material->diffuse);
				s.ps->SetShaderResourceView("normalTexture", material->normal);
				s.ps->SetShaderResourceView("specularTexture", material->spec);
				s.ps->SetShaderResourceView("aoTexture", material->ao);
				s.ps->SetShaderResourceView("armTexture", material->arm);
				s.ps->SetShaderResourceView("emissionTexture", material->emission);
				s.ps->SetShaderResourceView("opacityTexture", material->opacity);
				s.ps->CopyAllBufferData();

				context->OMSetBlendState(dx->no_blend, nullptr, 0xFFFFFFFF);
				context->OMSetDepthStencilState(dx->normal_depth, 0);
				context->RSSetState(dx->drawing_rasterizer);

				UINT stride = sizeof(Core::Vertex);
				UINT offset = 0;
				context->IASetVertexBuffers(0, 1, &s.vertex_buffer, &stride, &offset);
				context->IASetIndexBuffer(s.index_buffer, DXGI_FORMAT_R32_UINT, 0);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->DrawIndexed(s.index_count, 0, 0);

				t.dirty = false;
			}
		}

		ImTextureID Get(Core::MaterialData* material, int size) {
			if (material == nullptr || size <= 0) {
				return (ImTextureID)nullptr;
			}
			Core::DXCore* dx = Core::DXCore::Get();
			if (dx == nullptr || dx->device == nullptr || dx->context == nullptr) {
				return (ImTextureID)nullptr;
			}
			if (!EnsureShaders() || !EnsureSphere(dx->device)) {
				return (ImTextureID)nullptr;
			}

			State& s = Get_();
			Thumbnail& t = s.thumbnails[{ material->name, size }];
			const bool resized = (t.target.width != size);
			if (!t.target.Ensure(size, size)) {
				return (ImTextureID)nullptr;
			}
			if (t.dirty || resized) {
				RenderThumbnail(material, t);
			}
			return t.target.Handle();
		}

		void Invalidate(const std::string& material_name) {
			//Every size this material is cached at, not just one: the list thumbnail
			//and the detail preview are separate entries and both go stale together.
			for (auto& entry : Get_().thumbnails) {
				if (entry.first.first == material_name) {
					entry.second.dirty = true;
				}
			}
		}

		void InvalidateAll() {
			for (auto& entry : Get_().thumbnails) {
				entry.second.dirty = true;
			}
		}

		void Shutdown() {
			State& s = Get_();
			for (auto& entry : s.thumbnails) {
				entry.second.target.Release();
			}
			s.thumbnails.clear();
			RELEASE_PTR(s.vertex_buffer);
			RELEASE_PTR(s.index_buffer);
			s.index_count = 0;
			//The shaders belong to ShaderFactory, which owns their lifetime; only drop
			//our references so a later Shutdown/Get cycle re-fetches them.
			s.vs = nullptr;
			s.ps = nullptr;
			s.shaders_missing = false;
		}
	}
}
