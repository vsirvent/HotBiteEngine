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

#include "SimpleShader.h"
#include "Texture.h"
#include "Json.h"
#include "Utils.h"

#include <string>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace Core {

			class MaterialData;

			//Maximum number of layers one multi-material blends. Mirrored by
			//MAX_MULTI_TEXTURE in Shaders/Common/Defines.hlsli, which sizes both the
			//shader texture arrays and the packed constant arrays RenderSystem uploads -
			//raising it here alone would overrun those.
#define MAX_MULTI_TEXTURE 8

			//How a layer combines with the layers under it. Bits 0:1 of a layer's `op`;
			//mirrored by MULTITEXT_MIX/ADD/MULT in Shaders/Common/MultiTexture.hlsli.
#define TEXT_OP_MASK 3
#define TEXT_OP_MIX 1
#define TEXT_OP_ADD 2
#define TEXT_OP_MULT 3

			//Bits 3:9 - which maps the layer's source material actually provides. Derived
			//from the material, never authored (MultiMaterialData::Rebuild sets them).
#define TEXT_DIFF (1 << 3)
#define TEXT_NORM (1 << 4)
#define TEXT_SPEC (1 << 5)
#define TEXT_ARM  (1 << 6)
#define TEXT_DISP (1 << 7)
#define TEXT_AO   (1 << 8)
#define TEXT_MASK (1 << 9)

			//Bits 12: - the authored rules that decide *where* the layer lands.
#define TEXT_UV_NOISE   (1 << 12)
#define TEXT_MASK_NOISE (1 << 13)
			//Orientation rule on: narrow the layer to a range of dot(world normal, up).
#define TEXT_SLOPE      (1 << 14)
			//Altitude rule on: narrow the layer to a range of world-space Y.
#define TEXT_HEIGHT     (1 << 15)
			//Read the mask channel as 1 - value, so one image can drive two layers that
			//are each other's complement.
#define TEXT_MASK_INV   (1 << 16)
			//Bits 17:18 - which channel of the mask image this layer reads. Four layers
			//can share one RGBA "splat map", which is what makes a single big mask over a
			//terrain practical.
#define TEXT_MASK_CHANNEL_SHIFT 17
#define TEXT_MASK_CHANNEL_MASK (3 << TEXT_MASK_CHANNEL_SHIFT)

			//One layer of a MultiMaterialData: a material whose maps are blended onto the
			//surface, plus the rules that decide where. This is authoring data only - the
			//flattened, GPU-ready arrays live on MultiMaterialData and are derived from a
			//vector of these by Rebuild().
			struct MultiMaterialLayer {
				//Name of the material supplying this layer's diffuse/normal/spec/ao/arm/
				//height maps. Only its *maps* are used: the blend weight, uv scale and
				//masks below are the layer's own, and the base material of the surface
				//still supplies everything else (emission, opacity, shaders, flags).
				std::string material;
				//Mask image, relative to the world's assets path. Empty means no mask.
				std::string mask;
				//0=R 1=G 2=B 3=A. Which channel of `mask` this layer reads.
				int mask_channel = 0;
				bool mask_invert = false;

				//Where the mask image sits in UV space: the layer samples it at
				//`uv * mask_uv_scale + mask_uv_offset`. Deliberately *not* `uv_scale`,
				//which tiles the detail maps: the two want opposite frequencies. A rock
				//texture on a terrain repeats tens of times across it, while a splat map
				//painting where the paths go must cover the whole surface exactly once -
				//and a mesh's UVs are laid out for the first, so they run well outside
				//[0,1] (the demo terrain spans about 24 units each way). Left at 1/0 the
				//mask samples raw UV, which is what every mask did before this existed,
				//so nothing authored earlier moves.
				float mask_uv_scale = 1.0f;
				float2 mask_uv_offset = { 0.0f, 0.0f };
				//Modulate the mask by world-space noise, so a hand-painted boundary does
				//not read as a clean line.
				bool mask_noise = false;
				bool uv_noise = false;

				uint32_t op = TEXT_OP_MIX;
				float value = 1.0f;
				float uv_scale = 1.0f;

				//Orientation rule. The measured quantity is dot(world normal, up): 1 is
				//ground facing straight up, 0 a vertical wall, -1 an overhang. So snow is
				//[0.6, 1.0] and exposed cliff rock is [-1.0, 0.4]. `fade` is the half
				//width of the smooth edge at each end of the range, in the same units; 0
				//gives a hard line.
				bool slope_enabled = false;
				float slope_min = 0.0f;
				float slope_max = 1.0f;
				float slope_fade = 0.1f;

				//Altitude rule, the same shape, measured on the world-space Y of the
				//shaded point - the snow line, the waterline, the mud in the valley.
				bool height_enabled = false;
				float height_min = 0.0f;
				float height_max = 0.0f;
				float height_fade = 1.0f;
			};

			//A stack of material layers blended over a surface: a terrain painted with
			//dirt, grass and rock through one mask image, with snow on whatever faces up.
			//
			//Kept in two halves on purpose. `layers` is the authoring record - what the
			//editor edits, what a .mat file carries, what an undo restores. Everything
			//after it is the flattened form RenderSystem uploads, one entry per layer in
			//layer order, and Rebuild() is the only thing that writes it.
			//
			//A material *has* one of these (MaterialData::multi_material) rather than an
			//entity having one, because the render trees are keyed by material: every
			//entity in a bucket is drawn with one set of layer constants, so a per-entity
			//stack could only ever be honoured for whichever entity the bucket happened
			//to hold first.
			class MultiMaterialData {
			public:
				std::string name;
				std::vector<MultiMaterialLayer> layers;

				//Surface parameters the stack overrides on the material it is attached to.
				float multi_parallax_scale = 0.0f;
				uint32_t tessellation_type = 0;
				float tessellation_factor = 0.0f;
				float displacement_scale = 0.0f;

				//--- derived by Rebuild(), uploaded by RenderSystem::PrepareMultiMaterial
				std::vector<MaterialData*> multi_texture_data;
				std::vector<ID3D11ShaderResourceView*> multi_texture_mask;
				std::vector<uint32_t> multi_texture_operation;
				std::vector<float> multi_texture_value;
				std::vector<float> multi_texture_uv_scales;
				//Per layer, (min, max, fade, enabled) for the orientation and altitude
				//rules. float4 because an HLSL constant array strides by 16 bytes anyway,
				//so packing them tighter would buy nothing.
				std::vector<float4> multi_texture_slope;
				std::vector<float4> multi_texture_height;
				//Per layer, the mask's UV transform as (scale, scale, offset u, offset v).
				//Uniform scale in a float4 rather than two floats for the same reason as
				//above: the constant array strides by 16 bytes whatever is put in it.
				std::vector<float4> multi_texture_mask_uv;
				uint32_t multi_texture_count = 0;

				//Re-resolves every layer's source material and mask image and rebuilds the
				//arrays above. Call after any edit to `layers`. `root_path` is where mask
				//images are looked up (the world's assets path).
				//
				//Mask textures are taken from Core::LoadTexture's process-wide cache and
				//deliberately never released, exactly as the loader that came before this
				//did: a MultiMaterialData is copied by value into the tools that edit it,
				//and a released SRV would dangle in every copy. A repeat of the same path
				//is not reloaded (see `resolved_masks`), so dragging a slider is free.
				void Rebuild(const std::string& root_path,
					const FlatMap<std::string, MaterialData>& materials);

				//Binds `srv` as a layer's mask in place of whatever its `mask` file names,
				//until it is cleared with null. This is what lets a mask being painted show
				//up in the viewport as the brush moves: the alternative is writing a PNG and
				//reloading it per stroke. Survives Rebuild, and takes precedence over the
				//file, so an editing session is not undone by an unrelated layer edit.
				void SetLiveMask(uint32_t layer, ID3D11ShaderResourceView* srv);
				ID3D11ShaderResourceView* GetLiveMask(uint32_t layer) const;

				//The layer stack as a .mat "multi_materials" entry, and its inverse. The
				//file format predates this class (Tools/MaterialDesigner writes it), so
				//FromJson accepts the original keys and treats everything added since as
				//optional.
				nlohmann::json ToJson() const;
				void FromJson(const nlohmann::json& j, const std::string& root_path,
					const FlatMap<std::string, MaterialData>& materials);

				//FromJson over a serialized string. Kept because Tools/HotBiteTool drives
				//a live preview through it.
				bool LoadMultitexture(const std::string& json_str, const std::string& root_path,
					const FlatMap<std::string, MaterialData>& materials);

			private:
				//Absolute path each entry of multi_texture_mask was loaded from, so a
				//Rebuild that changed nothing does not hit the texture cache again.
				std::vector<std::string> resolved_masks;
				//Per layer, an editing tool's own mask texture (see SetLiveMask). Not
				//owned: the tool creates and destroys it, and clears the entry first.
				std::vector<ID3D11ShaderResourceView*> live_masks;
			};
			//Mirrored field for field by MaterialColor in Shaders/Common/PixelCommon.hlsli
			//and uploaded raw (RenderSystem binds it as the "material" cbuffer and as the
			//objectMaterials[] array the ray tracers index), so the layout has to obey HLSL
			//constant packing: no member may straddle a 16-byte row, and the trailing
			//padding keeps sizeof() equal to the row-padded size the shader array strides by.
			//Adding or removing a field here means editing MaterialColor to match.
			struct MaterialProps {
				float4 diffuseColor = {};

				float specIntensity = {};
				float parallax_scale = 0.0f;
				float parallax_steps = 4.0f;
				float parallax_angle_steps = 5.0f;

				float parallax_shadow_scale = 2.0f;
				float bloom_scale = 0.0f;
				float opacity = 1.0f;
				float density = 1.0f;

				float emission = 0.0f;
				float3 emission_color = {};

				float rt_reflex = 0.2f;
#define NORMAL_MAP_ENABLED_FLAG 1
#define PARALLAX_MAP_ENABLED_FLAG (1 << 1)
#define DIFFUSSE_MAP_ENABLED_FLAG (1 << 2)
#define SPEC_MAP_ENABLED_FLAG (1 << 3)
#define AO_MAP_ENABLED_FLAG (1 << 4)
#define ALPHA_ENABLED_FLAG (1 << 5)
#define ARM_MAP_ENABLED_FLAG (1 << 6)
#define EMISSION_MAP_ENABLED_FLAG (1 << 7)
#define OPACITY_MAP_ENABLED_FLAG (1 << 8)
#define BLEND_ENABLED_FLAG (1 << 10)
#define PARALLAX_SHADOW_ENABLED_FLAG (1 << 11)
#define RAY_TRACING_ENABLED_FLAG (1 << 12)
//World-aligned texture tiling (MainRenderPS.hlsli): samples the diffuse/normal/
//spec/ao/emission/opacity maps by a world-space planar projection along the
//surface's dominant axis instead of the mesh's authored UV, so scaling the
//entity changes how much geometry is covered rather than stretching the
//texture across it - see world_uv_scale below. Ordinary materials only; a
//multi-material's own per-layer uv_scale is unaffected.
#define WORLD_UV_ENABLED_FLAG (1 << 9)
				unsigned int flags = RAY_TRACING_ENABLED_FLAG;
				//World units per texture repeat when WORLD_UV_ENABLED_FLAG is set (a
				//tile size, not a frequency - bigger is coarser). Unused, and safely
				//ignored, otherwise. Replaces what used to be plain padding, so this
				//struct's size and HLSL row layout (mirrored by MaterialColor in
				//PixelCommon.hlsli) are unchanged.
				float world_uv_scale = 1.0f;
				float world_uv_reserved = 0.0f;
			};

			struct MaterialShaders {
				Core::SimpleVertexShader* vs = nullptr;
				Core::SimpleHullShader* hs = nullptr;
				Core::SimpleDomainShader* ds = nullptr;
				Core::SimpleGeometryShader* gs = nullptr;
				Core::SimplePixelShader* ps = nullptr;
			};

			//The .cso file names behind MaterialShaders. The resolved pointers alone
			//cannot answer "which shader is this material using?" - ShaderFactory hands
			//out shared instances and keeps no reverse mapping - so an editor that wants
			//to show or change a material's shaders needs the names kept alongside them.
			//These are the defaults MaterialData's constructor installs; Load() overwrites
			//them from the file and Save() writes them back.
			struct MaterialShaderNames {
				std::string draw_vs = "MainRenderVS.cso";
				std::string draw_hs = "MainRenderHS.cso";
				std::string draw_ds = "MainRenderDS.cso";
				std::string draw_gs = "MainRenderGS.cso";
				std::string draw_ps = "MainRenderPS.cso";
				std::string shadow_vs = "ShadowVS.cso";
				std::string shadow_gs = "ShadowMapCubeGS.cso";
				std::string depth_vs = "DepthVS.cso";
				std::string depth_ps = "DepthPS.cso";
			};

			struct MaterialTextures {
				std::string diffuse_texname;
				std::string normal_textname;
				std::string high_textname;
				std::string spec_textname;
				std::string ao_textname;
				std::string arm_textname;
				std::string emission_textname;
				std::string opacity_textname;
			};

			class MaterialData {
			private:
				void _MaterialData();
				void SetTexture(ID3D11ShaderResourceView*& texture, std::string& current_texture, const std::string& root, const std::string& file);
				void UpdateFlags();
			public:
				std::string name;
				ID3D11ShaderResourceView* diffuse = nullptr;
				ID3D11ShaderResourceView* normal = nullptr;
				ID3D11ShaderResourceView* high = nullptr;
				ID3D11ShaderResourceView* spec = nullptr;
				ID3D11ShaderResourceView* ao = nullptr;
				ID3D11ShaderResourceView* arm = nullptr;
				ID3D11ShaderResourceView* emission = nullptr;
				ID3D11ShaderResourceView* opacity = nullptr;

				MaterialProps props;
				//Absolute paths (root + file), not the bare file names the .mat carries.
				//Save() turns them back into root-relative names.
				MaterialTextures texture_names;
				MaterialShaders shaders;
				MaterialShaders shadow_shaders;
				MaterialShaders depth_shaders;
				MaterialShaderNames shader_names;
				int tessellation_type = 0;
				float tessellation_factor = 0.0f;
				float displacement_scale = 0.0f;

				//The layer stack this material draws with, or empty/null for a plain
				//material. The name is the authoring value, saved to and loaded from the
				//.mat file's "multi_material" key; the pointer is resolved against the
				//world's multi-material registry (World::ResolveMultiMaterials) and is
				//owned by it, never by the material.
				//
				//Non-null is what makes RenderSystem take the multi-texture path, so a
				//name that resolves to nothing leaves the material drawing as itself
				//rather than as an untextured surface.
				std::string multi_material_name;
				MultiMaterialData* multi_material = nullptr;

				bool init = false;

				//The JSON record this material was loaded from, kept verbatim so Save()
				//can write back the keys Load() does not consume - the legacy empty
				//"vs"/"hs"/"ds"/"gs"/"ps" entries, "normal_map_enabled", and any key a
				//future engine version adds. Without it, opening a .mat in the editor and
				//saving it would quietly drop or zero those fields. Empty for a material
				//created in code or in the editor, which then saves defaults only.
				//Keys of properties the engine has *retired* are dropped by Save() on
				//purpose (see the erase list there) rather than carried forever.
				nlohmann::json source_json;

				MaterialData();
				MaterialData(const std::string& name);
				MaterialData(const MaterialData& other);
				MaterialData& operator=(const MaterialData& other);
				~MaterialData();
				void Load(const std::string& root, const std::string& mat);
				//The inverse of Load: the material as a .mat "materials" array entry.
				//`root` is the texture folder the file declares; texture paths are written
				//relative to it, so a material whose textures live outside `root` keeps its
				//absolute path rather than silently pointing at the wrong file.
				nlohmann::json Save(const std::string& root) const;

				// Re-resolves every shader stage from `names` and, on success, adopts
				// them. All-or-nothing: if any name fails to load as the stage it was
				// asked for, nothing is changed and false is returned, so a bad pick can
				// never leave the material half-rebound and undrawable.
				//
				// Callers must re-register the entities using this material with the
				// render system afterwards (World::SetMaterialShaders does): its draw
				// trees are keyed by shader tuple, and nothing else notices the change.
				bool SetShaders(const MaterialShaderNames& names);
				bool Init();
				void Release();
			};

			void ReleaseTexture(ID3D11ShaderResourceView* srv);
			ID3D11ShaderResourceView* LoadTexture(const std::string& filename);
		}
	}
}