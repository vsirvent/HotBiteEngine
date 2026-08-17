// Scans the per-tile totals into per-tile base offsets, so every tile owns a
// contiguous slice of the entry pool. The only pass that has to see the whole frame at
// once, and the reason it is a single thread group.
//
// A general multi-block scan needs block sums and a third pass to add them back. There
// are only ~8k tiles at 1080p (~33k at 4K), so one group can cover them by giving each
// thread a contiguous run to sum serially, scanning the 256 partials in groupshared,
// and walking the runs again to write. That is two serial passes over tiles/256
// elements per thread - a few dozen - against three dispatches and a pair of scratch
// buffers.

#include "SplatCommon.hlsli"

cbuffer externalData : register(b0)
{
	uint tile_count;
	uint base_pad0;
	uint base_pad1;
	uint base_pad2;
}

Buffer<uint> tile_total : register(t0);
RWBuffer<uint> tile_base : register(u0);
RWByteAddressBuffer splat_stats : register(u1);

groupshared uint g_part[SPLAT_SCAN_GROUP];

[numthreads(SPLAT_SCAN_GROUP, 1, 1)]
void main(uint gi : SV_GroupIndex)
{
	// Contiguous runs rather than a strided assignment: each thread has to be able to
	// scan its own run serially in the second half, which needs the run to be adjacent.
	uint per = (tile_count + SPLAT_SCAN_GROUP - 1) / SPLAT_SCAN_GROUP;
	uint begin = min(gi * per, tile_count);
	uint end = min(begin + per, tile_count);

	uint sum = 0;
	uint i;
	for (i = begin; i < end; ++i) {
		sum += tile_total[i];
	}
	g_part[gi] = sum;
	GroupMemoryBarrierWithGroupSync();

	// Same Hillis-Steele shape as SplatScanCS, and the same barrier discipline: read,
	// sync, write, sync. The loop bound is uniform across the group, which is what lets
	// a sync sit inside it at all (fxc rejects one under divergent flow, X4026).
	for (uint off = 1; off < SPLAT_SCAN_GROUP; off <<= 1) {
		uint add = (gi >= off) ? g_part[gi - off] : 0u;
		GroupMemoryBarrierWithGroupSync();
		g_part[gi] += add;
		GroupMemoryBarrierWithGroupSync();
	}

	// Where this thread's run starts overall: the inclusive scan less its own total.
	uint run = g_part[gi] - sum;
	for (i = begin; i < end; ++i) {
		tile_base[i] = run;
		run += tile_total[i];
	}

	if (gi == SPLAT_SCAN_GROUP - 1) {
		// The grand total: what the pool must hold for this frame to be complete.
		// RenderSystem reads it back and grows, so an undersized pool costs a frame
		// rather than silently truncating for good.
		splat_stats.Store(8, g_part[gi]);
	}
}
