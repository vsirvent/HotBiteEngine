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
	// The window the tile-list sort key is quantized over: the near and far extent of
	// this cloud's own transformed bounding box from the camera. Fitted per cloud per
	// frame by RenderSystem, because quantizing over the camera's clip range instead
	// would put a whole cloud inside one or two of the 1022 steps.
	float  depth_quant_min;
	float  depth_quant_range;
	// Screen-coverage LOD: the fraction of this cloud's splats to keep this frame, 1 when
	// it is close enough to need all of them. RenderSystem fits it to the cloud's
	// projected area so the pass costs what the cloud covers rather than what it holds.
	float  splat_keep_prob;
	// 1 / splat_keep_prob. Restores the coverage the dropped splats would have
	// contributed - see the note at the alpha it multiplies.
	float  splat_alpha_comp;
	// Same slab the rasterizer uses, for the coarse reject against opaque geometry.
	float  splat_depth_slab;
	float  preprocess_pad0;
}

StructuredBuffer<SplatVertex> splats : register(t0);
// The depth pre-pass result: world distance to the nearest opaque surface. Used to
// reject a splat that is entirely behind geometry, which is the cheapest rejection
// available and the reason the rasterizer can afford a fixed slab test later.
Texture2D<float> depthTexture : register(t1);

RWStructuredBuffer<SplatView> splat_views : register(u0);
// Per tile, the quantized depth of the nearest splat touching it. SplatBinCS culls
// against this, which is the whole reason this pass no longer bins: the near depth of
// a tile is not known until every splat has been projected.
RWBuffer<uint> tile_depth : register(u1);

// A rejected splat is marked by a zero radius rather than left as whatever the last
// frame wrote - SplatBinCS reads this buffer rather than re-deriving the projection,
// so it needs to be able to tell a live entry from a stale one. Writing the single
// field costs 4 bytes against the 60 a full SplatView store would, which matters at
// a million-odd splats a frame.
void RejectSplat(uint idx)
{
	splat_views[idx].radius = 0.0f;
}

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint idx = tid.x;
	if (idx >= splat_count) {
		return;
	}

	// Screen-coverage LOD, before anything is loaded or projected - the whole point is to
	// not do that work.
	//
	// This is the one thing that makes the pass scale with distance. There is no natural
	// size cull: the low-pass floor a few lines below adds 0.3 to both diagonal terms of
	// the 2D covariance, so the smallest radius any splat can project to is
	// 3*sqrt(0.4) ~ 1.9 px however far away it is. Distance therefore never removes
	// splats, it only concentrates them - at 40 units this cloud still binned 3.4M
	// entries, but into 4 tiles instead of 255, so one tile held 1.24M of them and four
	// thread groups did all the work to produce 197 pixels. That is 183 ms of a 200 ms
	// frame.
	//
	// Dropping uniformly at random is close to free here, and specifically because the
	// far case is the oversampled one: the rasterizer takes an alpha-weighted *average*
	// of albedo and normal, and the mean of a random subset is the same mean. Coverage
	// (acc_w) does scale with the fraction kept, which is why the target is a few dozen
	// splats per pixel rather than one - well inside where saturate(acc_w) still pins
	// alpha to 1, so the cloud does not go translucent as it recedes.
	if (splat_keep_prob < 1.0f && SplatHash01(idx) >= splat_keep_prob) {
		RejectSplat(idx);
		return;
	}

	SplatVertex s = splats[idx];

	// Scaled by 1/keep_prob, so a kept splat stands for the ones dropped alongside it.
	//
	// Without this the LOD quietly changes *coverage* and not just sampling. The
	// rasterizer's albedo, normal and depth are all normalized by acc_w, so a random
	// subset gives the same answer for those - but alpha is saturate(acc_w) itself, and
	// acc_w falls with the fraction kept. Interior pixels are oversampled enough that
	// saturate() hides it; a thin tentacle or a silhouette is not, so its alpha slips
	// under surface_alpha, the G-buffer write is skipped and the terrain behind shows
	// through the object. Measured on the demo capture at the default density 16: 1.4% of
	// the cloud's pixels reverted to the background, and 9.1% at density 4.
	//
	// Deliberately unclamped. The estimator is unbiased - a splat kept with probability p
	// carries 1/p of the weight - so the mean coverage is right at any density; what a
	// low one costs is variance, which shows as a noisier edge rather than as holes in
	// the middle of the object.
	float alpha = s.opacity * splat_opacity_scale * splat_alpha_comp;
	if (alpha < SPLAT_MIN_ALPHA) {
		RejectSplat(idx);
		return;
	}

	float4 world_pos = mul(float4(s.position, 1.0f), world);
	float4 view_pos = mul(world_pos, view);
	// Behind or on the near plane: the projection divides by a number at or below
	// zero and the splat would land somewhere arbitrary on screen.
	if (view_pos.z <= near_plane) {
		RejectSplat(idx);
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
		RejectSplat(idx);
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
		RejectSplat(idx);
		return;
	}

	float2 lo = screen_xy - radius;
	float2 hi = screen_xy + radius;
	if (hi.x < 0.0f || hi.y < 0.0f || lo.x >= screenW || lo.y >= screenH) {
		RejectSplat(idx);
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
	if (view_depth > opaque_dist + splat_depth_slab) {
		RejectSplat(idx);
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

	// Record how near this splat is in every tile it touches. Nothing is binned here:
	// the cull SplatBinCS applies is against the *tile's* nearest splat, and that is
	// not known until every splat in the cloud has been projected. Hence the split -
	// this pass ends at a minimum, the next one bins against it.
	uint q = QuantizeSplatDepth(view_depth, depth_quant_min, depth_quant_range);

	int tx0 = max(0, (int)(lo.x) / SPLAT_TILE_SIZE);
	int ty0 = max(0, (int)(lo.y) / SPLAT_TILE_SIZE);
	int tx1 = min((int)tiles_x - 1, (int)(hi.x) / SPLAT_TILE_SIZE);
	int ty1 = min((int)tiles_y - 1, (int)(hi.y) / SPLAT_TILE_SIZE);

	for (int ty = ty0; ty <= ty1; ++ty) {
		for (int tx = tx0; tx <= tx1; ++tx) {
			InterlockedMin(tile_depth[ty * tiles_x + tx], q);
		}
	}
}
