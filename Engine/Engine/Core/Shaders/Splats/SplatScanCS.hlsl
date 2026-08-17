// Turns one tile's bucket histogram into one tile's bucket offsets. One group per
// *covered* tile, one thread per depth bucket.
//
// Dispatched indirectly over the list SplatCompactCS built, not over the screen grid:
// at 1080p the grid is 8160 tiles and a cloud is routinely in a handful of them, so the
// overwhelming majority of the groups existed to scan 128 zeros. Gating the work inside
// the shader was tried and does not help - the cost is the launch, not the scan - so
// the groups are not launched at all.
//
// Two-level, and this is the level that costs nothing: a tile has only
// SPLAT_DEPTH_BUCKETS counters, so its exclusive scan fits in a single group with no
// spilling and no inter-group communication. SplatBaseCS then scans the per-tile
// totals this leaves behind, which is the only part that has to see the whole frame.
// Splitting it that way avoids a general large-array scan entirely - the alternative
// is one scan over tiles * buckets elements, needing block sums and a third pass.

#include "SplatCommon.hlsli"

cbuffer externalData : register(b0)
{
	uint tile_count;
	uint scan_pad0;
	uint scan_pad1;
	uint scan_pad2;
}

// The covered tiles, densely packed by SplatCompactCS. This group's tile is the entry
// at its group index - the dispatch has exactly one group per entry.
Buffer<uint> tile_list : register(t0);

// In: this tile's per-bucket counts. Out: each bucket's offset within the tile.
RWBuffer<uint> bucket_offsets : register(u0);
// Out: how many entries this tile holds in total, which is what SplatBaseCS scans.
RWBuffer<uint> tile_total : register(u1);
// Counters for `splat_info`. Gathered here because this is the one dispatch that runs
// exactly once per tile and ends holding that tile's final total - counting during the
// binning would mean an atomic per (splat, tile) pair, millions onto one address.
RWByteAddressBuffer splat_stats : register(u2);

groupshared uint g_scan[SPLAT_DEPTH_BUCKETS];

[numthreads(SPLAT_DEPTH_BUCKETS, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
	// The bound is against a stale or oversized list rather than against the dispatch,
	// which is exact by construction; it costs one comparison and keeps a bad list from
	// scanning another tile's histogram.
	uint tile = tile_list[gid.x];
	if (tile >= tile_count) {
		return;
	}

	uint mine = bucket_offsets[tile * SPLAT_DEPTH_BUCKETS + gi];
	g_scan[gi] = mine;
	GroupMemoryBarrierWithGroupSync();

	// Hillis-Steele inclusive scan. The read has to complete for every thread before
	// any thread writes, hence the barrier between them rather than one per iteration -
	// with a single barrier this reads a value another thread has already advanced.
	for (uint off = 1; off < SPLAT_DEPTH_BUCKETS; off <<= 1) {
		uint add = (gi >= off) ? g_scan[gi - off] : 0u;
		GroupMemoryBarrierWithGroupSync();
		g_scan[gi] += add;
		GroupMemoryBarrierWithGroupSync();
	}

	// Inclusive minus own = exclusive, which is the offset this bucket starts at.
	bucket_offsets[tile * SPLAT_DEPTH_BUCKETS + gi] = g_scan[gi] - mine;

	if (gi == SPLAT_DEPTH_BUCKETS - 1) {
		uint total = g_scan[gi];
		tile_total[tile] = total;
		if (total > 0) {
			uint ignored;
			splat_stats.InterlockedAdd(0, 1, ignored);        // tiles holding anything
			splat_stats.InterlockedMax(4, total, ignored);    // the deepest tile
		}
	}
}
