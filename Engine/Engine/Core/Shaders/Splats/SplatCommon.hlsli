#ifndef __SPLAT_COMMON_HLSLI__
#define __SPLAT_COMMON_HLSLI__

// Shared layout for the Gaussian splat passes.
//
// SplatVertex mirrors Core::SplatVertex field for field. It is read as a
// StructuredBuffer, so the stride must match sizeof(SplatVertex) == 80 exactly and
// NOTHING VALIDATES THAT - a field added on one side and not the other shifts every
// subsequent read and renders as plausible-but-wrong geometry rather than as an
// error. Edit the two together.
struct SplatVertex
{
	float3 position;
	float  opacity;
	float3 cov_diag;        // Sxx, Syy, Szz
	float  spec_intensity;
	float3 cov_offdiag;     // Sxy, Sxz, Syz
	float  pad0;
	float3 albedo;
	float  pad1;
	float3 normal;
	float  pad2;
};

// What the preprocess pass hands the rasterizer: a splat already projected to
// screen space.
//
// 60 bytes: fxc packs a structured buffer element tightly on 4-byte boundaries,
// with none of the cbuffer float4 rules, so this is 15 floats and nothing more.
// RenderSystem::SplatView mirrors it field for field and static_asserts that
// number - if a field is added here, add it there too and re-read the stride out
// of the disassembly (`fxc /dumpbin SplatRasterCS.cso` ->
// `dcl_resource_structured t20, <stride>`) rather than trusting sizeof() on
// either side.
struct SplatView
{
	float2 screen_xy;   // projected centre, pixels
	float3 conic;       // inverse 2D covariance, upper triangle (a, b, c)
	float  view_depth;  // distance from the camera, world units
	float3 albedo;
	float3 normal;      // world space, already flipped if the component asked
	float  alpha;       // centre opacity, after the component's opacity_scale
	float  spec;
	float  radius;      // 3 sigma screen radius, pixels
};

// 16x16 pixels. Matches the thread group so one group owns one tile, and is the
// size the reference implementation settled on: 8x8 quadruples the group count and
// the per-tile setup cost, 32x32 is the DX11 group cap and leaves no headroom.
#define SPLAT_TILE_SIZE 16

// Per-tile list capacity. A tile that wants more than this drops the overflow
// rather than growing - DX11 cannot allocate mid-frame - and the drop is counted so
// splat_info can report it instead of it showing up as a hole in the cloud.
#define SPLAT_MAX_PER_TILE 1024

// Below this the Gaussian contributes under ~1/255 and is skipped. Not an
// occlusion test - it is a coverage test, and it is what lets the accumulation
// loops terminate early on the great majority of splat/pixel pairs.
#define SPLAT_MIN_ALPHA (1.0f / 255.0f)

// How far behind the nearest surface a splat may still contribute, in world units.
// This is what replaces a depth sort: everything inside the slab is averaged by
// weight, everything behind it is occluded. Too small and a curved surface loses
// its own silhouette splats; too large and the far side of a thin shell bleeds
// through.
#define SPLAT_DEPTH_SLAB 0.05f

// Evaluates the 2D Gaussian at `d` pixels from the splat centre.
float SplatWeight(float3 conic, float2 d)
{
	float power = -0.5f * (conic.x * d.x * d.x + conic.z * d.y * d.y) - conic.y * d.x * d.y;
	// power is <= 0 by construction for a positive-definite conic; the clamp is
	// against a degenerate one, where exp() of a positive number would explode.
	return exp(min(0.0f, power));
}

#endif
