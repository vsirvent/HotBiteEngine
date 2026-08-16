// Projects one Gaussian per thread into screen space and bins it into the tiles it
// covers. The first of the two splat passes; SplatRasterCS consumes what this
// writes.
//
// Why this exists as a separate dispatch: the rasterizer runs one group per tile
// and reads each splat once per tile it touches, so anything derivable per splat
// rather than per (splat, tile) pair belongs here - the world transform of the
// covariance and the projection Jacobian are the expensive parts and would
// otherwise be redone for every tile the splat straddles.

#include "SplatCommon.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	float3 cameraPosition;
	float  splat_opacity_scale;
	float3 albedo_tint;        // albedo_scale, broadcast
	float  splat_spec;
	uint   splat_count;
	uint   screenW;
	uint   screenH;
	uint   tiles_x;
	uint   tiles_y;
	uint   invert_normals;
	float  near_plane;
	float  far_plane;
}

StructuredBuffer<SplatVertex> splats : register(t0);
// The depth pre-pass result: world distance to the nearest opaque surface. Used to
// reject a splat that is entirely behind geometry, which is the cheapest rejection
// available and the reason the rasterizer can afford a fixed slab test later.
Texture2D<float> depthTexture : register(t1);

RWStructuredBuffer<SplatView> splat_views : register(u0);
RWBuffer<uint> tile_counts : register(u1);
RWBuffer<uint> tile_lists  : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint idx = tid.x;
	if (idx >= splat_count) {
		return;
	}

	SplatVertex s = splats[idx];

	float alpha = s.opacity * splat_opacity_scale;
	if (alpha < SPLAT_MIN_ALPHA) {
		return;
	}

	float4 world_pos = mul(float4(s.position, 1.0f), world);
	float4 view_pos = mul(world_pos, view);
	// Behind or on the near plane: the projection divides by a number at or below
	// zero and the splat would land somewhere arbitrary on screen.
	if (view_pos.z <= near_plane) {
		return;
	}

	float4 clip = mul(view_pos, projection);
	float2 ndc = clip.xy / clip.w;
	float2 screen_xy = float2((ndc.x * 0.5f + 0.5f) * screenW,
							  (0.5f - ndc.y * 0.5f) * screenH);

	// Sigma_world = M * Sigma * M^T, with M the world matrix's upper 3x3. Written
	// out because the covariance is stored as its upper triangle rather than as a
	// matrix: rebuilding a full float3x3 to multiply it and then discarding the
	// symmetric half costs more than this does.
	float3x3 M = (float3x3)world;
	float3x3 sigma = float3x3(
		s.cov_diag.x,    s.cov_offdiag.x, s.cov_offdiag.y,
		s.cov_offdiag.x, s.cov_diag.y,    s.cov_offdiag.z,
		s.cov_offdiag.y, s.cov_offdiag.z, s.cov_diag.z);
	float3x3 sigma_w = mul(mul(M, sigma), transpose(M));

	// The EWA projection: the 2D screen covariance is J W Sigma W^T J^T, with W the
	// view rotation and J the Jacobian of the perspective divide at this point.
	float3x3 W = (float3x3)view;
	float fx = projection._11 * screenW * 0.5f;
	float fy = projection._22 * screenH * 0.5f;
	float inv_z = 1.0f / view_pos.z;
	float inv_z2 = inv_z * inv_z;
	float3x3 J = float3x3(
		fx * inv_z, 0.0f,       -fx * view_pos.x * inv_z2,
		0.0f,       -fy * inv_z, fy * view_pos.y * inv_z2,
		0.0f,       0.0f,        0.0f);

	float3x3 T = mul(J, W);
	float3x3 cov2 = mul(mul(T, sigma_w), transpose(T));

	// A low-pass floor on the diagonal. Without it a splat seen edge-on projects to
	// a near-singular conic whose inverse blows up, and the splat draws as a bright
	// needle several pixels long - the classic 3DGS speckle.
	float a = cov2._11 + 0.3f;
	float b = cov2._12;
	float c = cov2._22 + 0.3f;

	float det = a * c - b * b;
	if (det <= 0.0f) {
		return;
	}
	float inv_det = 1.0f / det;
	// Inverse of the 2x2, which is what the rasterizer evaluates the Gaussian with.
	float3 conic = float3(c * inv_det, -b * inv_det, a * inv_det);

	// 3 sigma along the major axis: the larger eigenvalue of the 2x2, which for a
	// symmetric matrix is (tr/2) + sqrt((tr/2)^2 - det).
	float mid = 0.5f * (a + c);
	float lambda = mid + sqrt(max(0.01f, mid * mid - det));
	float radius = 3.0f * sqrt(lambda);

	// Under a third of a pixel it cannot change any pixel it lands on by more than
	// the alpha cutoff, and a distant cloud is mostly made of these.
	if (radius < 0.33f) {
		return;
	}

	float2 lo = screen_xy - radius;
	float2 hi = screen_xy + radius;
	if (hi.x < 0.0f || hi.y < 0.0f || lo.x >= screenW || lo.y >= screenH) {
		return;
	}

	// Coarse occlusion: if the splat's centre is well behind the opaque surface at
	// its own centre pixel it cannot contribute. Deliberately per splat and not per
	// pixel - it is a cheap reject, and the rasterizer's slab test does the accurate
	// version where it matters.
	int2 centre_px = int2(clamp(screen_xy.x, 0, (float)screenW - 1),
						  clamp(screen_xy.y, 0, (float)screenH - 1));
	float opaque_dist = depthTexture[centre_px];
	float view_depth = length(world_pos.xyz - cameraPosition);
	if (view_depth > opaque_dist + SPLAT_DEPTH_SLAB) {
		return;
	}

	SplatView v;
	v.screen_xy = screen_xy;
	v.conic = conic;
	v.view_depth = view_depth;
	v.albedo = s.albedo * albedo_tint;
	v.normal = normalize(mul(s.normal, M)) * (invert_normals ? -1.0f : 1.0f);
	v.alpha = alpha;
	v.spec = splat_spec;
	v.radius = radius;
	splat_views[idx] = v;

	// Bin. Order within a tile is irrelevant - the rasterizer is order-independent
	// by construction (min depth, then a weighted average inside a slab), which is
	// the whole reason there is no sort pass between here and it.
	int tx0 = max(0, (int)(lo.x) / SPLAT_TILE_SIZE);
	int ty0 = max(0, (int)(lo.y) / SPLAT_TILE_SIZE);
	int tx1 = min((int)tiles_x - 1, (int)(hi.x) / SPLAT_TILE_SIZE);
	int ty1 = min((int)tiles_y - 1, (int)(hi.y) / SPLAT_TILE_SIZE);

	for (int ty = ty0; ty <= ty1; ++ty) {
		for (int tx = tx0; tx <= tx1; ++tx) {
			uint tile = ty * tiles_x + tx;
			uint slot;
			InterlockedAdd(tile_counts[tile], 1, slot);
			if (slot < SPLAT_MAX_PER_TILE) {
				tile_lists[tile * SPLAT_MAX_PER_TILE + slot] = idx;
			}
			// Past capacity the count still climbs, on purpose: the rasterizer
			// clamps when it reads, and the excess is what tells the CPU the tile
			// overflowed rather than hiding it.
		}
	}
}
