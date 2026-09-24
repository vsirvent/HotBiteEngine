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

#include "SplatMeshReconstruction.h"
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <unordered_map>

namespace HotBite {
	namespace Engine {
		namespace Core {

			namespace {

				//Below this a splat is noise/stray as far as the surface goes - full opacity
				//is 1.0, and a real capture's meaningful geometry sits well above this floor.
				//A real capture's translucent artifacts ("floaters" - view-dependent haze a
				//3DGS trainer fits to explain away reflections/specular highlights it has no
				//other way to represent) commonly sit in the 0.05-0.15 range: low enough to
				//be invisible in the trained renders, high enough to survive a lenient floor.
				//MIN_SDF_NEIGHBOURS below is the primary defence (it catches a fully-opaque
				//isolated stray too), but there is no reason to feed the SDF ones this cheap
				//a filter already knows are suspect.
				constexpr float MIN_SPLAT_OPACITY = 0.15f;
				//Nearest splats averaged per grid corner. Small on purpose: this is an
				//oriented-plane distance estimate (Hoppe), not the PCA normal fit
				//SplatCloudData::Load already did once per splat - it only needs enough
				//neighbours to smooth sampling noise, not to rediscover the surface.
				constexpr int SDF_NEIGHBOURS = 8;
				//Below this many corroborating splats, a corner's estimate is not trusted
				//at all (treated as no-data/outside) even though candidates.empty() is
				//false. This is what keeps a real capture's floaters - the handful of
				//stray, physically isolated Gaussians every trained 3DGS scene has, often
				//low-opacity but not always - from each growing their own small phantom
				//island of "surface": a genuine patch of surface has hundreds of nearby
				//splats agreeing, an isolated floater has only itself (and maybe one or
				//two others as isolated as it is) for as far as the ring search reaches.
				constexpr int MIN_SDF_NEIGHBOURS = 4;
				//How far the search may widen (in grid cells) looking for SDF_NEIGHBOURS
				//candidates before giving up and marking a corner as outside/no-data.
				constexpr int SDF_MAX_RING = 6;
				//How far the NEAREST sample may be, in grid cells, before a corner's
				//estimate is refused as out of range - Hoppe's delta band, and the thing
				//that stops a few splats in a sparse region projecting their tangent planes
				//across empty space (see EvaluateSDF). Deliberately much smaller than
				//SDF_MAX_RING, which only bounds how far the search may LOOK for enough
				//candidates: finding them far away is what has to be rejected.
				constexpr float SDF_MAX_SAMPLE_CELLS = 1.25f;
				//A corner value this large can never cross zero against a real in-range
				//value, so it reads as "outside" without special-casing "no data" separately.
				constexpr float SENTINEL_DISTANCE = 1.0e6f;
				//Extra empty cells of margin around the cloud's own bounds, so a genuine
				//isosurface crossing is always strictly interior to the corner grid and every
				//crossing grid-edge has all four of its neighbouring cells in bounds - see
				//the quad-connectivity comment in BuildSurfaceNets.
				constexpr int GRID_PADDING_CELLS = 2;
				//Hard ceiling on the corner grid, independent of `grid_resolution`: a
				//pathological aspect ratio (a very flat, wide cloud) would otherwise ask for
				//an enormous number of cells along the short axes' padding alone.
				constexpr int64_t MAX_GRID_CELLS = 4 * 1024 * 1024;

				//A uniform grid over the (already opacity-filtered) splats, in CSR form -
				//the same shape as SplatCloud.cpp's own PointGrid, but sized directly from
				//the caller's cell size instead of derived from point density: this grid IS
				//the Surface Nets grid, so corner positions and splat buckets share one
				//indexing scheme.
				struct SplatGrid {
					float3 origin = {};
					float cell = 1.0f;
					int32_t dim[3] = { 1, 1, 1 }; //cells, not corners
					std::vector<uint32_t> cell_start;
					std::vector<uint32_t> splat_index;

					int32_t Axis(float v, float o, int32_t n) const {
						const int32_t c = (int32_t)floorf((v - o) / cell);
						return (c < 0) ? 0 : ((c >= n) ? n - 1 : c);
					}
					void CellOf(const float3& p, int32_t out[3]) const {
						out[0] = Axis(p.x, origin.x, dim[0]);
						out[1] = Axis(p.y, origin.y, dim[1]);
						out[2] = Axis(p.z, origin.z, dim[2]);
					}
					size_t CellLinear(int32_t x, int32_t y, int32_t z) const {
						return ((size_t)z * (size_t)dim[1] + (size_t)y) * (size_t)dim[0] + (size_t)x;
					}
					size_t CornerLinear(int32_t x, int32_t y, int32_t z) const {
						return ((size_t)z * (size_t)(dim[1] + 1) + (size_t)y) * (size_t)(dim[0] + 1) + (size_t)x;
					}
					float3 CornerPos(int32_t x, int32_t y, int32_t z) const {
						return { origin.x + (float)x * cell, origin.y + (float)y * cell, origin.z + (float)z * cell };
					}
				};

				void BuildSplatGrid(const std::vector<const SplatVertex*>& splats,
					const float3& min_dim, const float3& max_dim, int grid_resolution, SplatGrid& g) {
					const float ex = (std::max)(1.0e-4f, max_dim.x - min_dim.x);
					const float ey = (std::max)(1.0e-4f, max_dim.y - min_dim.y);
					const float ez = (std::max)(1.0e-4f, max_dim.z - min_dim.z);
					const float longest = (std::max)(ex, (std::max)(ey, ez));
					const int resolution = (std::max)(4, grid_resolution);
					g.cell = longest / (float)resolution;

					auto dims_for = [&](float c, int32_t d[3]) {
						d[0] = (std::max)(1, (int32_t)ceilf(ex / c)) + 2 * GRID_PADDING_CELLS;
						d[1] = (std::max)(1, (int32_t)ceilf(ey / c)) + 2 * GRID_PADDING_CELLS;
						d[2] = (std::max)(1, (int32_t)ceilf(ez / c)) + 2 * GRID_PADDING_CELLS;
						};
					dims_for(g.cell, g.dim);
					for (int guard = 0; guard < 64; ++guard) {
						const int64_t cells = (int64_t)g.dim[0] * (int64_t)g.dim[1] * (int64_t)g.dim[2];
						if (cells <= MAX_GRID_CELLS) { break; }
						g.cell *= 1.5f;
						dims_for(g.cell, g.dim);
					}
					//The padding is cells of margin, so the origin moves outward by that many
					//cells too - the real bounds start GRID_PADDING_CELLS cells in from corner 0.
					g.origin = { min_dim.x - GRID_PADDING_CELLS * g.cell,
						min_dim.y - GRID_PADDING_CELLS * g.cell, min_dim.z - GRID_PADDING_CELLS * g.cell };

					const size_t cells = (size_t)g.dim[0] * (size_t)g.dim[1] * (size_t)g.dim[2];
					g.cell_start.assign(cells + 1, 0);
					int32_t c[3];
					for (const SplatVertex* s : splats) {
						g.CellOf(s->position, c);
						++g.cell_start[g.CellLinear(c[0], c[1], c[2]) + 1];
					}
					for (size_t i = 1; i <= cells; ++i) {
						g.cell_start[i] += g.cell_start[i - 1];
					}
					std::vector<uint32_t> cursor(g.cell_start.begin(), g.cell_start.end() - 1);
					g.splat_index.resize(splats.size());
					for (uint32_t i = 0; i < (uint32_t)splats.size(); ++i) {
						g.CellOf(splats[i]->position, c);
						g.splat_index[cursor[g.CellLinear(c[0], c[1], c[2])]++] = i;
					}
				}

				//Hoppe's oriented-point signed distance at `p`: the opacity/distance-weighted
				//average of dot(p - splat.position, splat.normal) over the nearest splats
				//found by widening a block of grid cells around `p`. SENTINEL_DISTANCE when
				//nothing is found within SDF_MAX_RING - "no data", read as "outside".
				float EvaluateSDF(const SplatGrid& g, const std::vector<const SplatVertex*>& splats,
					const float3& p) {
					int32_t c[3];
					g.CellOf(p, c);
					std::vector<std::pair<float, uint32_t>> candidates;
					for (int32_t ring = 1; ring <= SDF_MAX_RING; ++ring) {
						const int32_t x0 = (std::max)(0, c[0] - ring), x1 = (std::min)(g.dim[0] - 1, c[0] + ring);
						const int32_t y0 = (std::max)(0, c[1] - ring), y1 = (std::min)(g.dim[1] - 1, c[1] + ring);
						const int32_t z0 = (std::max)(0, c[2] - ring), z1 = (std::min)(g.dim[2] - 1, c[2] + ring);
						candidates.clear();
						for (int32_t z = z0; z <= z1; ++z) {
							for (int32_t y = y0; y <= y1; ++y) {
								for (int32_t x = x0; x <= x1; ++x) {
									const size_t cell = g.CellLinear(x, y, z);
									for (uint32_t idx = g.cell_start[cell]; idx < g.cell_start[cell + 1]; ++idx) {
										const SplatVertex* s = splats[g.splat_index[idx]];
										const float3 d = SUB_F3_F3(s->position, p);
										candidates.push_back({ LENGHT_SQUARE_F3(d), g.splat_index[idx] });
									}
								}
							}
						}
						if ((int)candidates.size() >= SDF_NEIGHBOURS || (x0 == 0 && y0 == 0 && z0 == 0 &&
							x1 == g.dim[0] - 1 && y1 == g.dim[1] - 1 && z1 == g.dim[2] - 1)) {
							break;
						}
					}
					if ((int)candidates.size() < MIN_SDF_NEIGHBOURS) {
						//Too little agreement to trust - either genuinely empty space, or a
						//floater with too few (possibly zero other) neighbours to be real
						//surface. See MIN_SDF_NEIGHBOURS' own comment.
						return SENTINEL_DISTANCE;
					}
					const size_t take = (std::min)(candidates.size(), (size_t)SDF_NEIGHBOURS);
					std::partial_sort(candidates.begin(), candidates.begin() + take, candidates.end(),
						[](const auto& a, const auto& b) { return a.first < b.first; });

					//Hoppe's estimate is only defined NEAR the samples, and nothing above
					//enforces that: the count check is a count, and the value itself is the
					//distance to an INFINITE tangent plane, which stays near zero however
					//far `p` drifts SIDEWAYS from the splat that produced it. A corner a
					//long way to the side of a few splats therefore reads as "on the
					//surface" rather than as empty space.
					//
					//In a densely sampled region that never shows, because some splat is
					//always within a fraction of a cell. Where it does show is exactly where
					//a real capture is thin or sparse - a whip antenna, the outer edge of an
					//arm, a small cluster of floaters - and there the few splats found
					//project their tangent planes as far as the ring search reaches
					//(SDF_MAX_RING cells), which Surface Nets then extracts as a flat sheet
					//of triangles fanning out into empty space. That is the spiky "fin"
					//hanging off a reconstructed figure, and no amount of grid resolution
					//removes it: the fin scales with the search radius, not with the cell.
					//
					//So the estimate is refused outright once the NEAREST sample is further
					//than this, which is the delta band Hoppe defines the function over.
					//Generous enough (over a cell) that a legitimately coarse surface still
					//resolves - a corner that straddles the surface always has a sample
					//within about a cell of it, or the surface was not sampled there at all.
					if (candidates[0].first > (g.cell * SDF_MAX_SAMPLE_CELLS) * (g.cell * SDF_MAX_SAMPLE_CELLS)) {
						return SENTINEL_DISTANCE;
					}

					//The nearest candidate anchors which surface this point is being
					//evaluated against. When a SECOND, distinct surface also falls within
					//the search radius - two fingers close together, an arm near a torso,
					//any place a real capture's geometry comes within a few grid cells of
					//itself - its splats' normals roughly OPPOSE the nearest one's, because
					//two surfaces facing each other point their outward normals at each
					//other. Averaging both into one estimate is the textbook Hoppe-SDF
					//failure mode for close, disjoint surfaces: the value can read as
					//"inside" continuously across what should be open air between them,
					//fusing the two into one blob instead of leaving a gap. A single
					//coherent surface patch, even a curved one, keeps its normals within a
					//hemisphere of each other at this scale - the reconstruction is already
					//built on that assumption (see the class comment's "oriented tangent
					//planes") - so weighting each candidate by how well it agrees with the
					//nearest one's facing direction fades the other surface's splats out of
					//the estimate rather than blending them in at full strength.
					//
					//This is deliberately a smooth fade (agreement itself, clamped at zero)
					//and not a hard include/exclude cutoff: on a real capture a per-splat
					//normal is noisy, not just ambiguous between two surfaces - measured on
					//an imported 3DGS figure, ~7% of candidate/nearest pairs land in neither
					//the "clearly agrees" nor "clearly opposes" extreme, spread fairly evenly
					//in between rather than the clean bimodal split a noise-free surface
					//would give. A hard cutoff flips those borderline candidates in and out
					//of the average as `ref_n` (the nearest candidate, which itself changes
					//from one grid corner to the next) drifts across the threshold, which is
					//exactly the kind of corner-to-corner discontinuity that reads as a
					//jagged, spiky surface rather than a smooth or a cleanly absent one - and
					//it is worst precisely on thin, sparsely-sampled real structures (a whip
					//antenna, a raised arm) where there are few candidates to begin with, so
					//losing or gaining even one swings the result. A smooth fade has no such
					//cliff: a borderline candidate contributes a little instead of flipping
					//between all and nothing.
					//Deliberately NOT also gated on how MANY candidates agree. A thin
					//structure - a whip antenna, a strap, a cable - has only a splat or two
					//on the side facing the corner being evaluated, and the rest of what the
					//search finds is the far side of the same thin structure (opposing
					//normals, correctly faded out here) or a neighbouring surface it runs
					//close to. Requiring several agreeing samples blanks exactly those
					//corners, and the visible result is a thin feature that reconstructs in
					//floating pieces - an antenna whose base, where it passes closest to the
					//backpack it is mounted on, is missing entirely. The distance check
					//above is the guard that matters and is the better founded one: it asks
					//whether there is real data HERE, rather than how much of it happens to
					//face the same way.
					const float3 ref_n = splats[candidates[0].second]->normal;
					float weight_sum = 0.0f;
					float value_sum = 0.0f;
					for (size_t i = 0; i < take; ++i) {
						const SplatVertex* s = splats[candidates[i].second];
						const float d = DOT_F3_F3(s->normal, ref_n);
						if (d <= 0.0f) {
							continue;
						}
						const float dist2 = (std::max)(1.0e-8f, candidates[i].first);
						const float weight = (s->opacity / dist2) * d;
						const float3 to_p = SUB_F3_F3(p, s->position);
						value_sum += weight * DOT_F3_F3(to_p, s->normal);
						weight_sum += weight;
					}
					return (weight_sum > 0.0f) ? (value_sum / weight_sum) : SENTINEL_DISTANCE;
				}

				//Decides which of the "no data" corners are INSIDE the object and flips them
				//to a large negative, so they read as solid rather than as empty space.
				//
				//Every capture is a SHELL - splats sit on the visible surface and there is
				//nothing behind them - so EvaluateSDF only has an answer within the delta
				//band either side of that surface. Deeper in than that it returns
				//SENTINEL_DISTANCE, which is positive, i.e. "outside". The corners just
				//inside the shell are negative, so the deep interior and the shell's inner
				//face disagree in sign, and Surface Nets duly extracts a surface between
				//them: a second, INNER shell shadowing the real one, hidden inside the mesh
				//and roughly doubling its triangles where the body is thicker than the band.
				//Measured on an imported figure it was 5.9% of the quads at a coarse grid
				//and 32.8% at a fine one - it gets worse the finer the grid, because the
				//band is a multiple of the cell and so shrinks with it while the body does
				//not.
				//
				//Which no-data corners are interior cannot be read off the value (the same
				//SENTINEL means "beyond the band" both inside and out), but it can be read
				//off CONNECTIVITY: the grid is padded with GRID_PADDING_CELLS of empty cells
				//(BuildSplatGrid), so its boundary is certainly outside, and any no-data
				//corner reachable from there through other no-data corners is outside too.
				//Whatever is left over is enclosed by the band, and is the interior.
				//
				//Reachability specifically, and not "whichever face of the band is nearest",
				//which is the obvious-looking alternative and is worse. Handing every
				//no-data corner the side of the nearest band face does fill an interior the
				//band fails to enclose - but it also puts the two labels next to each other
				//wherever they meet, and a corner marked inside against one marked outside
				//is itself a sign change, so Surface Nets extracts the seam between them as
				//yet another surface. Measured on an imported figure that made things worse,
				//not better (15.5% of quads against 2.9% at a coarse grid). An enclosed
				//region cannot do that: being unreachable, it is separated from the outside
				//by known band corners everywhere, so the two sentinels are never adjacent.
				//
				//Degrades gracefully: a capture with a genuine hole in it - nothing was ever
				//observed under the boots of a figure standing on the ground - lets the fill
				//in through it, which simply leaves that object as it was rather than making
				//it worse.
				void MarkEnclosedNoData(const SplatGrid& g, std::vector<float>& values) {
					const int32_t nx = g.dim[0] + 1, ny = g.dim[1] + 1, nz = g.dim[2] + 1;
					auto is_no_data = [&](size_t i) { return values[i] >= SENTINEL_DISTANCE * 0.5f; };

					std::vector<uint8_t> outside(values.size(), 0);
					std::vector<uint32_t> stack;
					auto visit = [&](int32_t x, int32_t y, int32_t z) {
						const size_t i = g.CornerLinear(x, y, z);
						if (!outside[i] && is_no_data(i)) {
							outside[i] = 1;
							stack.push_back((uint32_t)i);
						}
						};

					for (int32_t z = 0; z < nz; ++z) {
						for (int32_t y = 0; y < ny; ++y) {
							for (int32_t x = 0; x < nx; ++x) {
								if (x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == nz - 1) {
									visit(x, y, z);
								}
							}
						}
					}
					while (!stack.empty()) {
						const uint32_t i = stack.back();
						stack.pop_back();
						const int32_t x = (int32_t)(i % (uint32_t)nx);
						const int32_t y = (int32_t)((i / (uint32_t)nx) % (uint32_t)ny);
						const int32_t z = (int32_t)(i / ((uint32_t)nx * (uint32_t)ny));
						if (x > 0) { visit(x - 1, y, z); }
						if (x + 1 < nx) { visit(x + 1, y, z); }
						if (y > 0) { visit(x, y - 1, z); }
						if (y + 1 < ny) { visit(x, y + 1, z); }
						if (z > 0) { visit(x, y, z - 1); }
						if (z + 1 < nz) { visit(x, y, z + 1); }
					}

					for (size_t i = 0; i < values.size(); ++i) {
						if (is_no_data(i) && !outside[i]) {
							values[i] = -SENTINEL_DISTANCE;
						}
					}
				}

				//Surface Nets (Gibson 1998): one vertex per cell whose 8 corners disagree in
				//sign, at the average of that cell's sign-crossing edges; quads emitted along
				//grid edges that cross the surface, connecting the (up to) four cells around
				//that edge. See SplatMeshReconstruction.h for why this over Marching Cubes.
				//
				//The four neighbours of a crossing edge are only ever in range because the
				//caller's grid pads GRID_PADDING_CELLS empty cells around the cloud's real
				//bounds (BuildSplatGrid) - every real crossing is therefore strictly interior
				//to the corner lattice, so subtracting 1 from either transverse index below
				//never goes negative and adding 1 never reaches the lattice edge.
				void BuildSurfaceNets(const SplatGrid& g, const std::vector<float>& values,
					std::vector<Vertex>& out_vertices, std::vector<uint32_t>& out_indices) {
					const int32_t dx = g.dim[0], dy = g.dim[1], dz = g.dim[2];
					auto corner_value = [&](int32_t x, int32_t y, int32_t z) {
						return values[g.CornerLinear(x, y, z)];
						};
					auto cell_index = [&](int32_t x, int32_t y, int32_t z) {
						return ((size_t)z * (size_t)dy + (size_t)y) * (size_t)dx + (size_t)x;
						};

					std::vector<int32_t> cell_vertex((size_t)dx * (size_t)dy * (size_t)dz, -1);

					//Corner offsets of a unit cube's 8 corners and the 12 edges between them,
					//as pairs of corner indices into that offset list - the standard cube
					//numbering used by every marching-cubes/surface-nets writeup.
					static const int32_t CORNER_OFFSET[8][3] = {
						{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
					};
					static const int32_t EDGE[12][2] = {
						{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
					};

					for (int32_t z = 0; z < dz; ++z) {
						for (int32_t y = 0; y < dy; ++y) {
							for (int32_t x = 0; x < dx; ++x) {
								float cv[8];
								bool has_neg = false, has_pos = false;
								for (int i = 0; i < 8; ++i) {
									cv[i] = corner_value(x + CORNER_OFFSET[i][0], y + CORNER_OFFSET[i][1],
										z + CORNER_OFFSET[i][2]);
									if (cv[i] < 0.0f) { has_neg = true; }
									else { has_pos = true; }
								}
								if (!has_neg || !has_pos) { continue; }

								float3 sum = { 0.0f, 0.0f, 0.0f };
								int count = 0;
								for (int e = 0; e < 12; ++e) {
									const float v0 = cv[EDGE[e][0]];
									const float v1 = cv[EDGE[e][1]];
									if ((v0 < 0.0f) == (v1 < 0.0f)) { continue; }
									const float t = v0 / (v0 - v1);
									const float3 p0 = g.CornerPos(x + CORNER_OFFSET[EDGE[e][0]][0],
										y + CORNER_OFFSET[EDGE[e][0]][1], z + CORNER_OFFSET[EDGE[e][0]][2]);
									const float3 p1 = g.CornerPos(x + CORNER_OFFSET[EDGE[e][1]][0],
										y + CORNER_OFFSET[EDGE[e][1]][1], z + CORNER_OFFSET[EDGE[e][1]][2]);
									sum = ADD_F3_F3(sum, ADD_F3_F3(p0, MULT_F3_F(SUB_F3_F3(p1, p0), t)));
									++count;
								}
								if (count == 0) { continue; }
								Vertex v{};
								v.Position = DIV_F3(sum, (float)count);
								cell_vertex[cell_index(x, y, z)] = (int32_t)out_vertices.size();
								out_vertices.push_back(v);
							}
						}
					}

					//One quad (2 triangles) per grid edge that crosses the surface, connecting
					//the vertices of the (up to) four cells sharing that edge. `axis` picks
					//which lattice edges are being walked (0=x,1=y,2=z); `along` indexes the
					//varying coordinate and `t0`/`t1` the two fixed ones, matching the layout
					//BuildSplatGrid/corner_value use for all three axes uniformly.
					auto walk_axis = [&](int axis) {
						const int32_t lens[3] = { dx, dy, dz };
						const int32_t along_len = lens[axis];
						const int32_t t0_len = lens[(axis + 1) % 3];
						const int32_t t1_len = lens[(axis + 2) % 3];
						for (int32_t t1 = 1; t1 < t1_len; ++t1) {
							for (int32_t t0 = 1; t0 < t0_len; ++t0) {
								for (int32_t along = 0; along < along_len; ++along) {
									int32_t c0[3], c1[3];
									c0[axis] = along; c0[(axis + 1) % 3] = t0; c0[(axis + 2) % 3] = t1;
									c1[axis] = along + 1; c1[(axis + 1) % 3] = t0; c1[(axis + 2) % 3] = t1;
									const float v0 = corner_value(c0[0], c0[1], c0[2]);
									const float v1 = corner_value(c1[0], c1[1], c1[2]);
									if ((v0 < 0.0f) == (v1 < 0.0f)) { continue; }

									//The four cells around this edge, walked in cyclic order in
									//the (t0,t1) plane so they form a non-crossing quad.
									int32_t cell_coord[4][3];
									const int32_t offs[4][2] = { {-1,-1}, {0,-1}, {0,0}, {-1,0} };
									for (int q = 0; q < 4; ++q) {
										cell_coord[q][axis] = along;
										cell_coord[q][(axis + 1) % 3] = t0 + offs[q][0];
										cell_coord[q][(axis + 2) % 3] = t1 + offs[q][1];
									}
									int32_t quad[4];
									bool all_active = true;
									for (int q = 0; q < 4; ++q) {
										quad[q] = cell_vertex[cell_index(cell_coord[q][0], cell_coord[q][1], cell_coord[q][2])];
										if (quad[q] < 0) { all_active = false; break; }
									}
									if (!all_active) { continue; }

									//v0 < 0 (start inside, end outside): keep winding. Otherwise
									//(start outside, end inside) reverse it, so the two cases -
									//which are mirror images of each other along this edge -
									//both wind their triangles to face away from the inside.
									if (v0 < 0.0f) {
										out_indices.push_back(quad[0]); out_indices.push_back(quad[1]); out_indices.push_back(quad[2]);
										out_indices.push_back(quad[0]); out_indices.push_back(quad[2]); out_indices.push_back(quad[3]);
									}
									else {
										out_indices.push_back(quad[0]); out_indices.push_back(quad[2]); out_indices.push_back(quad[1]);
										out_indices.push_back(quad[0]); out_indices.push_back(quad[3]); out_indices.push_back(quad[2]);
									}
								}
							}
						}
						};
					walk_axis(0);
					walk_axis(1);
					walk_axis(2);
				}

				//Welds vertices Surface Nets placed at (or extremely near) the same position -
				//two separate cells whose few sign-crossing edges happen to average to the
				//identical point, which a thin/sparse region (many cells right at the
				//MIN_SDF_NEIGHBOURS boundary) produces often enough to matter on a real,
				//messy capture. Left alone these do not look wrong in a screenshot, but a
				//cluster of many duplicate/near-duplicate positions can make
				//Core::BVH::Subdivide's mean-based split put every one of them on the same
				//side of its own split value - see BVH.cpp's own comment on why that used to
				//recurse forever building this exact kind of generated mesh. Also drops the
				//triangles a weld makes degenerate (two or more corners now the same
				//vertex), which a duplicate cluster produces alongside.
				void WeldDuplicateVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
					float epsilon) {
					if (!(epsilon > 0.0f) || vertices.empty()) {
						return;
					}
					const float inv_epsilon = 1.0f / epsilon;
					auto quantize = [&](float v) { return (int64_t)floorf(v * inv_epsilon); };
					struct Key { int64_t x, y, z; };
					struct KeyHash {
						size_t operator()(const Key& k) const {
							size_t h = std::hash<int64_t>()(k.x);
							h = h * 1099511628211ull ^ std::hash<int64_t>()(k.y);
							h = h * 1099511628211ull ^ std::hash<int64_t>()(k.z);
							return h;
						}
					};
					struct KeyEq {
						bool operator()(const Key& a, const Key& b) const {
							return a.x == b.x && a.y == b.y && a.z == b.z;
						}
					};

					std::unordered_map<Key, uint32_t, KeyHash, KeyEq> canonical;
					canonical.reserve(vertices.size() * 2);
					std::vector<uint32_t> remap(vertices.size());
					std::vector<Vertex> welded;
					welded.reserve(vertices.size());
					for (uint32_t i = 0; i < (uint32_t)vertices.size(); ++i) {
						const Key key{ quantize(vertices[i].Position.x), quantize(vertices[i].Position.y),
							quantize(vertices[i].Position.z) };
						auto it = canonical.find(key);
						if (it == canonical.end()) {
							const uint32_t new_index = (uint32_t)welded.size();
							canonical.emplace(key, new_index);
							welded.push_back(vertices[i]);
							remap[i] = new_index;
						}
						else {
							remap[i] = it->second;
						}
					}

					std::vector<uint32_t> welded_indices;
					welded_indices.reserve(indices.size());
					for (size_t i = 0; i + 2 < indices.size(); i += 3) {
						const uint32_t a = remap[indices[i]], b = remap[indices[i + 1]], c = remap[indices[i + 2]];
						if (a == b || b == c || a == c) {
							continue;
						}
						welded_indices.push_back(a);
						welded_indices.push_back(b);
						welded_indices.push_back(c);
					}
					vertices = std::move(welded);
					indices = std::move(welded_indices);
				}

				void ComputeFaceNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
					std::vector<float3> accum(vertices.size(), float3{ 0.0f, 0.0f, 0.0f });
					for (size_t i = 0; i + 2 < indices.size(); i += 3) {
						const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
						const float3 e1 = SUB_F3_F3(vertices[ib].Position, vertices[ia].Position);
						const float3 e2 = SUB_F3_F3(vertices[ic].Position, vertices[ia].Position);
						const float3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
						accum[ia] = ADD_F3_F3(accum[ia], n);
						accum[ib] = ADD_F3_F3(accum[ib], n);
						accum[ic] = ADD_F3_F3(accum[ic], n);
					}
					for (size_t i = 0; i < vertices.size(); ++i) {
						const float len2 = LENGHT_SQUARE_F3(accum[i]);
						vertices[i].Normal = (len2 > 1.0e-12f) ? UNIT_F3(accum[i]) : float3{ 0.0f, 1.0f, 0.0f };
					}
				}
			}

			bool ReconstructSplatMesh(const std::vector<SplatVertex>& splats,
				const float3& min_dim, const float3& max_dim,
				int grid_resolution, float target_ratio,
				SimplifyResult& out, std::string& error) {
				out = SimplifyResult{};

				std::vector<const SplatVertex*> filtered;
				filtered.reserve(splats.size());
				for (const SplatVertex& s : splats) {
					if (s.opacity >= MIN_SPLAT_OPACITY) {
						filtered.push_back(&s);
					}
				}
				if (filtered.size() < 4) {
					error = "not enough opaque splats to reconstruct a surface";
					return false;
				}

				SplatGrid grid;
				BuildSplatGrid(filtered, min_dim, max_dim, grid_resolution, grid);

				std::vector<float> values((size_t)(grid.dim[0] + 1) * (size_t)(grid.dim[1] + 1) * (size_t)(grid.dim[2] + 1));
				for (int32_t z = 0; z <= grid.dim[2]; ++z) {
					for (int32_t y = 0; y <= grid.dim[1]; ++y) {
						for (int32_t x = 0; x <= grid.dim[0]; ++x) {
							values[grid.CornerLinear(x, y, z)] = EvaluateSDF(grid, filtered, grid.CornerPos(x, y, z));
						}
					}
				}
				//The interior of a shell is "no data", not "outside" - see the function.
				MarkEnclosedNoData(grid, values);

				std::vector<Vertex> raw_vertices;
				std::vector<uint32_t> raw_indices;
				BuildSurfaceNets(grid, values, raw_vertices, raw_indices);
				if (raw_indices.empty()) {
					error = "the reconstructed isosurface was empty - try a finer grid resolution";
					return false;
				}
				//A small fraction of the grid cell size - enough to catch floating-point-
				//coincident or extremely close averages a thin/sparse region produces,
				//without merging genuinely distinct nearby surface vertices.
				WeldDuplicateVertices(raw_vertices, raw_indices, grid.cell * 1.0e-3f);
				if (raw_indices.empty()) {
					error = "welding left no triangles - the reconstructed isosurface was degenerate";
					return false;
				}
				ComputeFaceNormals(raw_vertices, raw_indices);

				if (!(target_ratio > 0.0f) || target_ratio >= 1.0f) {
					out.vertices = std::move(raw_vertices);
					out.indices = std::move(raw_indices);
					return true;
				}
				if (!SimplifyMesh(raw_vertices, raw_indices, target_ratio, out)) {
					//Not fatal - the un-decimated reconstruction is still a valid, if less
					//"low poly", proxy. SimplifyMesh already logs why it declined.
					out.vertices = std::move(raw_vertices);
					out.indices = std::move(raw_indices);
				}
				return true;
			}
		}
	}
}
