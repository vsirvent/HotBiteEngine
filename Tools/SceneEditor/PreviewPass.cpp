#include "PreviewPass.h"

#include <Core/DXCore.h>

#include <DirectXMath.h>

using namespace HotBite::Engine;
using namespace DirectX;

namespace HotBiteEditor {
	namespace PreviewPass {
		namespace {

			//Where each light sits on the camera's own axes: (right, up, towards the
			//viewer). The key is in front, above and to the right - the direction a
			//viewer reads as daylight - the fill answers it from the left at a third of
			//the strength, and the back light is behind and above the subject, which is
			//the one that keeps a dark model off a dark background.
			constexpr float KEY_RIG[3]  = { 0.55f,  0.60f,  0.70f };
			constexpr float FILL_RIG[3] = { -0.85f, 0.15f,  0.35f };
			constexpr float BACK_RIG[3] = { 0.25f,  0.45f, -0.85f };

			XMVECTOR Compose(const float weights[3], FXMVECTOR right, FXMVECTOR up,
				FXMVECTOR towards_viewer) {
				return XMVector3Normalize(
					XMVectorAdd(XMVectorAdd(XMVectorScale(right, weights[0]),
						XMVectorScale(up, weights[1])),
						XMVectorScale(towards_viewer, weights[2])));
			}
		}

		LightRig MakeLightRig(const float3& eye, const float3& target) {
			XMVECTOR forward = XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&eye));
			if (XMVectorGetX(XMVector3LengthSq(forward)) < 1e-12f) {
				//A camera sitting on its own target has no view direction to build on;
				//any consistent one will do, and this is the identity view's.
				forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			}
			forward = XMVector3Normalize(forward);

			//Left-handed, the same basis XMMatrixLookAtLH builds: right = up x forward.
			XMVECTOR right = XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward);
			if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-6f) {
				//Looking straight up or down: the world up axis is no longer a reference,
				//so pick the horizontal axis it degenerated away from.
				right = XMVector3Cross(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), forward);
			}
			right = XMVector3Normalize(right);
			const XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
			const XMVECTOR towards_viewer = XMVectorNegate(forward);

			LightRig rig;
			XMStoreFloat3(&rig.key, Compose(KEY_RIG, right, up, towards_viewer));
			XMStoreFloat3(&rig.fill, Compose(FILL_RIG, right, up, towards_viewer));
			XMStoreFloat3(&rig.back, Compose(BACK_RIG, right, up, towards_viewer));
			return rig;
		}

		void BindLightRig(Core::SimplePixelShader* ps, const LightRig& rig) {
			if (ps == nullptr) {
				return;
			}
			ps->SetFloat3("keyDir", rig.key);
			ps->SetFloat3("fillDir", rig.fill);
			ps->SetFloat3("backDir", rig.back);
		}

		bool Target::Ensure(int w, int h) {
			if (w <= 0 || h <= 0) {
				Release();
				return false;
			}
			if (texture != nullptr && width == w && height == h) {
				return true;
			}
			Release();

			Core::DXCore* dx = Core::DXCore::Get();
			if (dx == nullptr || dx->device == nullptr) {
				return false;
			}
			GfxDevice* device = dx->device;

			D3D12_RESOURCE_DESC desc{};
				desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				desc.Width = (UINT)w;
				desc.Height = (UINT)h;
				desc.MipLevels = 1;
				desc.DepthOrArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				if (FAILED(device->CreateTexture2D(desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, 0, &texture)) ||
					FAILED(device->CreateRTV(texture, nullptr, &rtv)) ||
					FAILED(device->CreateSRV(texture, nullptr, &srv))) {
					Release();
					return false;
				}

				D3D12_RESOURCE_DESC depth_desc = desc;
				depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
				if (FAILED(device->CreateTexture2D(depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, nullptr, 0, &depth_texture)) ||
					FAILED(device->CreateDSV(depth_texture, nullptr, &dsv))) {
				Release();
				return false;
			}

			width = w;
			height = h;
			return true;
		}

		void Target::Bind(const float clear_color[4]) const {
			if (!Valid()) {
				return;
			}
			GfxContext* context = Core::DXCore::Get()->context;

			D3D12_VIEWPORT viewport{};
			viewport.Width = (float)width;
			viewport.Height = (float)height;
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);

			//OMSetRenderTargets takes an array, so the view has to be addressable.
			GfxRTV* targets[1] = { rtv };
			context->OMSetRenderTargets(1, targets, dsv);
			context->ClearRenderTargetView(rtv, clear_color);
			context->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
		}

		void Target::Release() {
			RELEASE_PTR(srv);
			RELEASE_PTR(rtv);
			RELEASE_PTR(texture);
			RELEASE_PTR(dsv);
			RELEASE_PTR(depth_texture);
			width = 0;
			height = 0;
		}

		ScopedState::ScopedState() {
			GfxContext* context = Core::DXCore::Get()->context;
			context->OMGetRenderTargets(1, &prev_rtv, &prev_dsv);
			context->RSGetViewports(&prev_viewport_count, &prev_viewport);

			//See the header: a preview mesh is never tessellated, so the scene's
			//hull/domain/geometry stages must not run over it.
			context->HSSetShader(nullptr);
			context->DSSetShader(nullptr);
			context->GSSetShader(nullptr);
		}

		ScopedState::~ScopedState() {
			GfxContext* context = Core::DXCore::Get()->context;

			//Material textures off the pixel stage before the target goes back - see
			//the header for the read/write hazard this avoids. MaterialPreviewPS binds
			//seven, and a mesh normal map makes eight.
			GfxSRV* null_srvs[8] = {};
			context->PSSetShaderResources(0, 8, null_srvs);

			context->OMSetRenderTargets(1, &prev_rtv, prev_dsv);
			if (prev_viewport_count > 0) {
				context->RSSetViewports(1, &prev_viewport);
			}
			RELEASE_PTR(prev_rtv);
			RELEASE_PTR(prev_dsv);
		}
	}
}
