// Builds the list of tiles a cloud actually covers, and the DispatchIndirect argument
// that launches one thread group per entry of it. One thread per tile.
//
// Why this exists. SplatScanCS and SplatRasterCS both want one group per tile, and both
// used to be dispatched over the whole screen grid - 8160 groups at 1080p - while a
// cloud typically occupies a handful of them (the binning measurements have tiles_used
// falling to single digits once a cloud is a few tens of units away). Everything else
// was already gated: the rasterizer reads tile_total and returns on zero. What no gate
// inside a shader can remove is the *launch*, and a dispatch whose groups do nothing is
// bounded by how fast the hardware can launch and retire them. Compacting first turns
// "launch 8160 groups, 8156 of which retire immediately" into "launch as many groups as
// there are tiles", which is the only version of this idea that can pay.
//
// It is one thread per tile rather than one group, so the pass this replaces at group
// granularity costs 32 groups of 256 threads instead of 8160 groups of 128.
//
// tile_depth is a sound occupancy flag rather than an approximation of one:
// SplatPreprocessCS's InterlockedMin and SplatBinCS's histogram enumerate the same tile
// range, derived from the same screen_xy +/- radius of the same SplatView, so a tile
// still holding the sentinel is exactly a tile that received no InterlockedAdd - i.e.
// one whose tile_total would have come out zero. It is also free: the buffer is cleared
// and filled for the binning either way.

#include "SplatCommon.hlsli"

cbuffer externalData : register(b0)
{
	uint tile_count;
	uint compact_pad0;
	uint compact_pad1;
	uint compact_pad2;
}

// The tile's nearest splat, quantized, or SPLAT_NO_DEPTH for a tile no splat reaches.
Buffer<uint> tile_depth : register(t0);

// Out: the indices of the covered tiles, densely packed. Order is whatever the atomics
// produced and nothing downstream cares - a tile's work depends only on its own slice.
RWBuffer<uint> tile_list : register(u0);
// Out, for the tiles that are NOT listed. SplatScanCS writes this for every tile it
// scans, so with the scan now running only over the list, the tiles left out of it have
// to be zeroed here. Skipping that would be a real bug rather than a missing
// optimisation: nothing clears this buffer between frames, and SplatBaseCS sums it over
// the whole grid, so one stale total left behind displaces the pool slice of every tile
// after it.
RWBuffer<uint> tile_total : register(u1);
// Out: ThreadGroupCountX/Y/Z for the two dispatches this feeds. Element 0 is both the
// list's cursor and the group count - the InterlockedAdd that allocates a slot is what
// builds the argument, so there is no second pass and no readback. Cleared to zero each
// frame, hence the Y and Z below.
RWBuffer<uint> dispatch_args : register(u2);

[numthreads(SPLAT_COMPACT_GROUP, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint tile = tid.x;
	if (tile >= tile_count) {
		return;
	}

	// Before the early-out below, or a frame whose tile 0 happens to be empty would
	// leave the argument buffer's Y and Z at the zero the clear put there and launch
	// nothing at all.
	if (tile == 0) {
		dispatch_args[1] = 1;
		dispatch_args[2] = 1;
	}

	if (tile_depth[tile] == SPLAT_NO_DEPTH) {
		tile_total[tile] = 0;
		return;
	}

	uint slot;
	InterlockedAdd(dispatch_args[0], 1, slot);
	tile_list[slot] = tile;
}
