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

#include <Defines.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include "BVH.h"
#include "DXCore.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			/**
			 * One Gaussian of a splat cloud, as the GPU sees it.
			 *
			 * This is deliberately NOT the representation a 3D Gaussian Splatting paper
			 * describes. There, a splat stores baked outgoing *radiance* - spherical
			 * harmonics fitted to how the surface looked under the lighting of whatever
			 * capture produced it - and the renderer's job ends at compositing it.
			 *
			 * Here a splat stores *material*: an albedo, a surface normal and a specular
			 * intensity, exactly the things MainRenderPS feeds CalcDirectional/CalcPoint
			 * with. The splat rasterizer then lights it with the scene's own lights and
			 * shadows, so a splat cloud sits under the same sun as the geometry beside it,
			 * takes the same shadows, and reacts when the level's lighting changes. A baked
			 * cloud does none of that: it is lit for one moment of one day forever, and it
			 * is the reason splat captures normally look pasted into a game scene.
			 *
			 * The cost is that an imported capture's f_dc coefficients are being
			 * *reinterpreted* as albedo. They are not albedo - they are radiance with the
			 * capture's lighting already multiplied in - so a cloud captured in hard
			 * sunlight carries baked highlights and shadows that the engine will then light
			 * a second time. See SplatCloudData::Load for what is and is not done about it.
			 *
			 * 80 bytes. The layout is padded to float4 boundaries so the HLSL
			 * StructuredBuffer stride matches without the compiler re-packing anything;
			 * SplatCommon.hlsli mirrors it field for field and nothing validates that, so
			 * the two must be edited together.
			 */
			struct SplatVertex {
				//Model-space centre of the Gaussian.
				float3 position = {};
				//Alpha at the centre, already through the sigmoid the .ply stores it behind.
				float opacity = 1.0f;

				//The 3D covariance, upper triangle, pre-baked from the file's scale and
				//rotation at import: Sigma = R * S * S^T * R^T.
				//
				//Baked rather than kept as scale(3)+quat(4) because the per-frame transform
				//is then Sigma' = M * Sigma * M^T, two 3x3 multiplies that handle non-uniform
				//entity scale correctly. The scale/quat form buys nothing until per-splat
				//animation exists, which it does not.
				float3 cov_diag = {};        //Sxx, Syy, Szz
				float spec_intensity = 0.0f;

				float3 cov_offdiag = {};     //Sxy, Sxz, Syz
				float pad0 = 0.0f;

				//Base colour, linear. See the class comment on what this actually is.
				float3 albedo = {};
				float pad1 = 0.0f;

				//Model-space surface normal, derived at import as the minor axis of the
				//covariance ellipsoid - see SplatCloudData::Load. A .ply's own nx/ny/nz are
				//present in the format and are almost always zero or noise, so they are not
				//trusted.
				float3 normal = { 0.0f, 1.0f, 0.0f };
				float pad2 = 0.0f;
			};

			static_assert(sizeof(SplatVertex) == 80, "SplatVertex must stay 80 bytes; SplatCommon.hlsli strides by it");

			/**
			 * A splat cloud asset: the shared, immutable geometry that any number of
			 * entities can draw, and the analogue of Core::MeshData for the splat path.
			 *
			 * Like MeshData this is asset scope, not entity scope - two entities drawing one
			 * cloud share every splat of it, and anything per entity (the transform, the
			 * opacity scale) lives on the component instead.
			 *
			 * Unlike MeshData it owns its GPU buffer. Mesh geometry lives in the world's one
			 * IMMUTABLE vertex buffer and a MeshData is only a pair of offsets into it, but
			 * splats never reach the input assembler at all: they are read as a
			 * StructuredBuffer by a compute shader, so they get a buffer of their own and
			 * there is nothing to share.
			 */
			class SplatCloudData {
			private:
				std::string name;
				std::string source_file;
				//Immutable structured SRV, created directly from `splats` rather than
				//through Core::Buffer<T>. Buffer<T> keeps a vector of its own and Add()
				//copies into it, which for a real capture is a second 150 MB allocation
				//live at the same time as the first, for no gain - nothing here ever
				//appends, and the data is already contiguous.
				ID3D11Buffer* buffer = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
				std::vector<SplatVertex> splats;
				uint32_t splat_count = 0;
				bool prepared = false;

			public:
				//Bounds of the cloud in model space, measured at 3 sigma rather than over
				//the splat centres - a Gaussian is not a point, and one at the edge of the
				//cloud extends past its own centre by several times its scale. Measuring
				//centres gives a box the cloud visibly pokes out of, which culls early and
				//reads as popping at the screen edge.
				float3 min_dimensions = {};
				float3 max_dimensions = {};

				SplatCloudData() = default;
				~SplatCloudData();

				const std::string& GetName() const { return name; }
				const std::string& GetSourceFile() const { return source_file; }
				//Held separately from splats.size(), which goes to zero once the CPU copy
				//is released.
				uint32_t Count() const { return splat_count; }
				const std::vector<SplatVertex>& Splats() const { return splats; }
				//Pointer-to-SRV so it can be handed straight to CSSetShaderResources.
				ID3D11ShaderResourceView* const* SRV() const { return &srv; }
				bool Prepared() const { return prepared; }

				//Frees the CPU copy once the GPU has it. A capture is hundreds of
				//megabytes and nothing on this side reads it back - the rasterizer works
				//entirely from the SRV. Kept as an explicit call rather than done inside
				//Prepare because the bounds and the splat count are derived before it and
				//a future CPU-side consumer (a collider fit, a save-back) would want the
				//data still there.
				void ReleaseCpuCopy();

				/**
				 * Reads a binary-little-endian .ply in the layout the reference 3DGS
				 * implementation emits, and every trainer and capture tool after it:
				 * x/y/z, nx/ny/nz, f_dc_0..2, f_rest_0..44, opacity, scale_0..2, rot_0..3.
				 *
				 * Five conversions happen here rather than per frame, and each one is a
				 * decode the format requires rather than a choice:
				 *
				 *  - opacity is stored pre-sigmoid, so it is put through the logistic.
				 *  - scales are stored as logs, so they are exponentiated.
				 *  - the rotation quaternion is stored (w,x,y,z) and unnormalized.
				 *  - f_dc are SH degree-0 coefficients, so colour is 0.5 + C0 * f_dc.
				 *  - the file is in the trainer's COLMAP frame - right-handed, Y *down* -
				 *    and this engine is left-handed Y-up, so Y is negated on the position,
				 *    the normal and the covariance. Without it a capture loads upside down
				 *    and mirrored, which reads as a broken mesh rather than as a frame
				 *    mismatch.
				 *
				 * Then two that ARE choices, and are the whole point of this component:
				 *
				 *  - that colour is kept as *albedo*, not as radiance. Nothing tries to
				 *    remove the capture's baked lighting: de-lighting a radiance field is an
				 *    open research problem, not something to attempt in a loader. A cloud
				 *    captured under flat overcast light reinterprets well; one captured in
				 *    hard sun keeps its baked highlights and gets lit twice. albedo_scale on
				 *    the component is the practical escape hatch.
				 *  - the normal is the minor axis of the covariance ellipsoid. Gaussians
				 *    train flat - they spread along a surface and collapse across it - so the
				 *    shortest of the three scale axes, rotated into model space, points along
				 *    the surface normal. Sign is genuinely ambiguous (an ellipsoid has no
				 *    facing) and is resolved toward the cloud centroid, which is right for a
				 *    scan of an object seen from outside and wrong for one seen from inside.
				 */
				bool Load(const std::string& file, const std::string& asset_name);

				/**
				 * Fills this cloud with a generated sphere of splats, normals facing out and
				 * albedo taken from the normal. The stand-in for a SplatCloud component with
				 * no asset chosen, and the automation suite's fixture - a generated cloud
				 * means the splat tests need no binary .ply checked into the repo.
				 *
				 * The splats are flattened across the radius (the radial scale is a fraction
				 * of the tangential ones) so the minor-axis normal derivation the importer
				 * uses has something meaningful to find here too, rather than the default
				 * cloud being the one case where normals come from somewhere else.
				 */
				void BuildDefault(const std::string& asset_name);

				//Uploads to the GPU. Separate from Load so the import can run off the render
				//thread; Prepare must not.
				HRESULT Prepare();
				void Unprepare();
			};
		}
	}
}
