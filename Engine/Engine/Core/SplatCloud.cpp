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

#include "SplatCloud.h"
#include "Log.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

//float3 is a HotBite::Engine typedef, not a ::Core one. Load's own body reaches it
//through the enclosing namespace of SplatCloudData, but the file-scope helpers below
//are in an anonymous namespace and need it brought in explicitly.
using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

namespace {

	//The SH degree-0 basis function. A .ply's f_dc_* are coefficients against it, so
	//the colour they encode is 0.5 + C0 * f_dc - the 0.5 being the DC offset the
	//reference implementation trains around.
	constexpr float SH_C0 = 0.28209479177387814f;

	//How many standard deviations of a Gaussian the bounds should contain. Past 3
	//sigma a Gaussian contributes under 1% and the rasterizer's own alpha cutoff
	//removes it, so this is where the cloud visually ends.
	constexpr float BOUNDS_SIGMA = 3.0f;

	//How much of the outward direction a normal must carry before "point it away from
	//the cloud centroid" is taken as evidence of anything. Below this the two are near
	//enough to perpendicular that the sign is being read off the fit's error rather
	//than off the geometry. 0.1 is about 6 degrees off perpendicular - well under the
	//error of a plane fitted to a noisy scan, and far enough from the ~1.0 a closed
	//object's surface gives that it never fires on one.
	constexpr float NORMAL_CENTROID_MIN_COS = 0.1f;

	//How much of a Gaussian's own trained (minor-axis) normal survives the blend with
	//the neighbourhood fit below - the rest, 1 - this, is the fit. The trained axis is
	//the shortest axis of a fitted ellipsoid, not a direct measurement of the surface,
	//and comes out visibly noisier than the neighbourhood agrees on; the fit alone is
	//what a plain point cloud already uses (see EstimatePointCloudNormals below), so a
	//low weight here just means "mostly trust the same thing a point cloud does,
	//nudged by what the actual training settled on".
	constexpr float GAUSSIAN_NORMAL_WEIGHT = 0.3f;

	struct PlyProperty {
		std::string name;
		//Size in bytes of the scalar. Only float/double/int-family appear in the splat
		//layout, but the header is parsed generically so an unexpected property can be
		//skipped by width rather than aborting the load.
		uint32_t size = 0;
		bool is_float = false;
		bool is_double = false;
	};

	uint32_t PlyTypeSize(const std::string& type, bool& is_float, bool& is_double) {
		is_float = false;
		is_double = false;
		if (type == "float" || type == "float32") { is_float = true; return 4; }
		if (type == "double" || type == "float64") { is_double = true; return 8; }
		if (type == "char" || type == "int8" || type == "uchar" || type == "uint8") { return 1; }
		if (type == "short" || type == "int16" || type == "ushort" || type == "uint16") { return 2; }
		if (type == "int" || type == "int32" || type == "uint" || type == "uint32") { return 4; }
		return 0;
	}

	float Sigmoid(float x) {
		return 1.0f / (1.0f + expf(-x));
	}

	//--- Normal estimation for a plain point cloud (and a Gaussian's own blend) ----
	//
	// A trained Gaussian carries its own normal: it is flat against the surface it
	// fits, so the minor axis of its ellipsoid points across that surface. In practice
	// that axis is noisier than the neighbourhood agrees on - it comes from whatever
	// the fit settled on for one splat, not a measurement of the surface - so it is
	// blended with the same neighbourhood fit a plain point cloud relies on entirely
	// (GAUSSIAN_NORMAL_WEIGHT above), rather than trusted alone. A plain coloured
	// point cloud carries no shape per point at all - no scale_*, no rot_* -
	// so every splat falls back to an identity rotation and an isotropic scale, which
	// is a *sphere*, and a sphere has no minor axis. The tie-break in the derivation
	// then picks the same basis column for every point in the file, so the whole cloud
	// comes out with one normal, split into two halves by the sign resolution. That is
	// what "the normals are broken" looks like, and no amount of fixing the derivation
	// helps: the information is not in the point.
	//
	// It is in the *neighbouring* points. This fits a plane to each point's k nearest
	// neighbours and takes its normal - the standard estimator (PCL, Open3D and
	// MeshLab all do this), and the eigenvector of the smallest eigenvalue of the
	// neighbourhood's covariance is exactly that plane's normal: the direction the
	// neighbourhood is thinnest in, which for points sampled off a surface is across
	// it.
	//
	// Note this is the covariance of *where the neighbours are*, and has nothing to do
	// with SplatVertex::cov_diag/cov_offdiag, which is the shape of one Gaussian. The
	// two are unrelated quantities that happen to share a name.

	//Neighbours per fit. Enough that the plane is fitted to the surface rather than to
	//the scanner's noise, few enough that it stays local - a larger neighbourhood
	//starts rounding off real creases, which is the one error that cannot be filtered
	//out afterwards.
	constexpr uint32_t PCA_NEIGHBOURS = 24;

	//Neighbours per point kept for the sign-propagation graph (see
	//OrientNormalsConsistently). Fewer than the fit uses, and for a different reason:
	//the fit wants enough samples to average the noise out, the graph wants edges short
	//enough that the two normals really are describing the same piece of surface. It is
	//also what that pass costs in memory, at 4 bytes each per point.
	constexpr uint32_t PCA_ORIENT_NEIGHBOURS = 8;

	//Ceiling on the acceleration grid. The cell size below is derived from the point
	//spacing, which is cubic in the extent and overshoots badly on a real capture (a
	//million points across a room asks for tens of millions of cells, almost all of
	//them empty air). Past this the cell grows instead; the only cost is more
	//candidates to sift per query.
	constexpr size_t PCA_MAX_CELLS = 2u * 1024u * 1024u;

	//How far the cell block around a point may be widened looking for neighbours. Only
	//reached where the cloud is far sparser than average - an outlier off on its own.
	constexpr int32_t PCA_MAX_RINGS = 8;

	//Below this the threads cost more to start than the fit costs to run.
	constexpr size_t PCA_MIN_THREADED = 8192;

	//A uniform grid over the point set, in CSR form: the points of cell c are
	//point_index[cell_start[c] .. cell_start[c+1]). Chosen over a k-d tree because a
	//scan is near-uniformly dense, which is the case a grid is built for, and because
	//the build is two counting passes rather than a recursive partition.
	struct PointGrid {
		float3 origin = {};
		float cell = 1.0f;
		int32_t dim[3] = { 1, 1, 1 };
		std::vector<uint32_t> cell_start;
		std::vector<uint32_t> point_index;

		int32_t Axis(float v, float o, int32_t n) const {
			const int32_t c = (int32_t)floorf((v - o) / cell);
			return (c < 0) ? 0 : ((c >= n) ? n - 1 : c);
		}
		void CellOf(const float3& p, int32_t out[3]) const {
			out[0] = Axis(p.x, origin.x, dim[0]);
			out[1] = Axis(p.y, origin.y, dim[1]);
			out[2] = Axis(p.z, origin.z, dim[2]);
		}
		size_t Linear(int32_t x, int32_t y, int32_t z) const {
			return ((size_t)z * (size_t)dim[1] + (size_t)y) * (size_t)dim[0] + (size_t)x;
		}
	};

	void BuildPointGrid(const std::vector<SplatVertex>& pts, PointGrid& g) {
		float3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
		float3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const SplatVertex& s : pts) {
			bmin.x = fminf(bmin.x, s.position.x); bmax.x = fmaxf(bmax.x, s.position.x);
			bmin.y = fminf(bmin.y, s.position.y); bmax.y = fmaxf(bmax.y, s.position.y);
			bmin.z = fminf(bmin.z, s.position.z); bmax.z = fmaxf(bmax.z, s.position.z);
		}
		g.origin = bmin;

		const float ex = fmaxf(0.0f, bmax.x - bmin.x);
		const float ey = fmaxf(0.0f, bmax.y - bmin.y);
		const float ez = fmaxf(0.0f, bmax.z - bmin.z);
		const float longest = fmaxf(ex, fmaxf(ey, ez));

		//Scanned points lie on a *surface*, not through a volume, so the spacing between
		//neighbours goes as extent/sqrt(N) rather than the extent/cbrt(N) a volume fill
		//would give. Three spacings to a cell puts a handful of points in each, so the
		//3x3x3 block searched below holds several times PCA_NEIGHBOURS.
		g.cell = (longest > 0.0f) ? (float)(3.0 * (double)longest / sqrt((double)pts.size())) : 1.0f;
		//Degenerate input - every point coincident, or a NaN in the file. Any positive
		//cell works: the grid collapses to one cell and every query sees every point.
		if (!(g.cell > 0.0f)) { g.cell = 1.0f; }

		auto dims_for = [&](float c, int32_t d[3]) {
			d[0] = (int32_t)floorf(ex / c) + 1;
			d[1] = (int32_t)floorf(ey / c) + 1;
			d[2] = (int32_t)floorf(ez / c) + 1;
		};
		dims_for(g.cell, g.dim);
		for (int guard = 0; guard < 64; ++guard) {
			const double cells = (double)g.dim[0] * (double)g.dim[1] * (double)g.dim[2];
			if (cells <= (double)PCA_MAX_CELLS) { break; }
			g.cell *= 1.5f;
			dims_for(g.cell, g.dim);
		}

		//Counting sort into the CSR layout: count into the slot one past the cell, prefix
		//sum in place so cell_start is its own offset table, then scatter through a
		//cursor copy.
		const size_t cells = (size_t)g.dim[0] * (size_t)g.dim[1] * (size_t)g.dim[2];
		g.cell_start.assign(cells + 1, 0);
		int32_t c[3];
		for (const SplatVertex& s : pts) {
			g.CellOf(s.position, c);
			++g.cell_start[g.Linear(c[0], c[1], c[2]) + 1];
		}
		for (size_t i = 1; i <= cells; ++i) {
			g.cell_start[i] += g.cell_start[i - 1];
		}
		std::vector<uint32_t> cursor(g.cell_start.begin(), g.cell_start.end() - 1);
		g.point_index.resize(pts.size());
		for (uint32_t i = 0; i < (uint32_t)pts.size(); ++i) {
			g.CellOf(pts[i].position, c);
			g.point_index[cursor[g.Linear(c[0], c[1], c[2])]++] = i;
		}
	}

	//Eigenvector of the smallest eigenvalue of a symmetric 3x3, by cyclic Jacobi. The
	//matrix is destroyed.
	//
	//Closed-form eigenvalues plus a cross product is faster and is what a lot of normal
	//estimators reach for, but it loses the eigenvector in exactly the case this needs
	//it most: a neighbourhood flat enough that its two in-plane eigenvalues are nearly
	//equal, which is every well-sampled planar patch. Jacobi has no such case.
	float3 SmallestEigenvector(float a[3][3]) {
		float v[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		for (int sweep = 0; sweep < 16; ++sweep) {
			if (fabsf(a[0][1]) + fabsf(a[0][2]) + fabsf(a[1][2]) < 1e-20f) { break; }
			for (int p = 0; p < 2; ++p) {
				for (int q = p + 1; q < 3; ++q) {
					const float apq = a[p][q];
					if (fabsf(apq) < 1e-20f) { continue; }
					const float theta = (a[q][q] - a[p][p]) / (2.0f * apq);
					const float t = ((theta >= 0.0f) ? 1.0f : -1.0f) /
						(fabsf(theta) + sqrtf(theta * theta + 1.0f));
					const float cs = 1.0f / sqrtf(t * t + 1.0f);
					const float sn = t * cs;
					const int r = 3 - p - q;
					const float arp = a[r][p];
					const float arq = a[r][q];
					a[p][p] -= t * apq;
					a[q][q] += t * apq;
					a[p][q] = a[q][p] = 0.0f;
					a[r][p] = a[p][r] = cs * arp - sn * arq;
					a[r][q] = a[q][r] = sn * arp + cs * arq;
					for (int k = 0; k < 3; ++k) {
						const float vkp = v[k][p];
						const float vkq = v[k][q];
						v[k][p] = cs * vkp - sn * vkq;
						v[k][q] = sn * vkp + cs * vkq;
					}
				}
			}
		}
		int m = 0;
		if (a[1][1] < a[m][m]) { m = 1; }
		if (a[2][2] < a[m][m]) { m = 2; }
		return float3{ v[0][m], v[1][m], v[2][m] };
	}

	//Fills out_axis with an un-oriented surface normal per point - an axis, with the
	//sign still to be resolved, exactly like the minor axis of a Gaussian - and
	//out_neighbours with each point's nearest PCA_ORIENT_NEIGHBOURS (UINT32_MAX where a
	//point has fewer), which is the graph OrientNormalsConsistently propagates the sign
	//along. The neighbours are free here: the search has already gathered and
	//partitioned the candidates, and finding them again later would mean a second pass
	//over the whole cloud.
	//
	//Positions are read in whatever frame they are already in, so running this after the
	//loader's Y flip gives normals in engine space with no second flip needed (a mirror
	//applied to the points is inherited exactly by a plane fitted through them).
	void EstimatePointCloudNormals(const std::vector<SplatVertex>& pts,
		std::vector<float3>& out_axis, std::vector<uint32_t>& out_neighbours) {
		out_axis.assign(pts.size(), float3{ 0.0f, 1.0f, 0.0f });
		out_neighbours.assign(pts.size() * (size_t)PCA_ORIENT_NEIGHBOURS, UINT32_MAX);
		//Three points define the plane; below that there is nothing to fit and every
		//point keeps the default.
		if (pts.size() < 4) { return; }

		PointGrid grid;
		BuildPointGrid(pts, grid);

		const size_t k = (pts.size() - 1 < (size_t)PCA_NEIGHBOURS) ? pts.size() - 1 : (size_t)PCA_NEIGHBOURS;

		auto worker = [&](size_t first, size_t last) {
			//Reused across points: this runs once per point of a capture that may hold
			//millions, and a per-point allocation would dominate the pass.
			std::vector<std::pair<float, uint32_t>> cand;
			for (size_t i = first; i < last; ++i) {
				const float3& p = pts[i].position;
				int32_t c[3];
				grid.CellOf(p, c);

				//Widen the block of cells until it holds enough candidates. Deliberately
				//approximate: a true k-NN would keep widening until the k-th distance fits
				//inside the block. Not worth it - the fit averages a whole neighbourhood,
				//so swapping one far neighbour for another at a similar distance moves the
				//plane by far less than the scan's own noise already does.
				for (int32_t ring = 1; ring <= PCA_MAX_RINGS; ++ring) {
					const int32_t x0 = (c[0] - ring > 0) ? c[0] - ring : 0;
					const int32_t y0 = (c[1] - ring > 0) ? c[1] - ring : 0;
					const int32_t z0 = (c[2] - ring > 0) ? c[2] - ring : 0;
					const int32_t x1 = (c[0] + ring < grid.dim[0] - 1) ? c[0] + ring : grid.dim[0] - 1;
					const int32_t y1 = (c[1] + ring < grid.dim[1] - 1) ? c[1] + ring : grid.dim[1] - 1;
					const int32_t z1 = (c[2] + ring < grid.dim[2] - 1) ? c[2] + ring : grid.dim[2] - 1;

					cand.clear();
					for (int32_t z = z0; z <= z1; ++z) {
						for (int32_t y = y0; y <= y1; ++y) {
							const size_t row = grid.Linear(x0, y, z);
							for (int32_t x = x0; x <= x1; ++x) {
								const size_t cellidx = row + (size_t)(x - x0);
								for (uint32_t e = grid.cell_start[cellidx]; e < grid.cell_start[cellidx + 1]; ++e) {
									const uint32_t j = grid.point_index[e];
									if ((size_t)j == i) { continue; }
									const float dx = pts[j].position.x - p.x;
									const float dy = pts[j].position.y - p.y;
									const float dz = pts[j].position.z - p.z;
									cand.emplace_back(dx * dx + dy * dy + dz * dz, j);
								}
							}
						}
					}
					if (cand.size() >= k) { break; }
					//The block already covers the whole grid - widening it again would
					//re-scan the same points forever.
					if (x0 == 0 && y0 == 0 && z0 == 0 &&
						x1 == grid.dim[0] - 1 && y1 == grid.dim[1] - 1 && z1 == grid.dim[2] - 1) {
						break;
					}
				}
				if (cand.size() < 3) { continue; }

				const size_t take = (k < cand.size()) ? k : cand.size();
				if (take < cand.size()) {
					std::nth_element(cand.begin(), cand.begin() + take, cand.end(),
						[](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) {
							return a.first < b.first;
						});
				}

				//The closest few of those, in order, for the orientation graph. nth_element
				//only partitions, so the k nearest are in the front of the range but in no
				//order within it - the graph wants the *nearest* neighbours specifically,
				//since an edge between two points far apart on a curved surface is exactly
				//the one whose sign should not be trusted.
				const size_t keep = (take < (size_t)PCA_ORIENT_NEIGHBOURS) ? take : (size_t)PCA_ORIENT_NEIGHBOURS;
				std::partial_sort(cand.begin(), cand.begin() + keep, cand.begin() + take,
					[](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) {
						return a.first < b.first;
					});
				uint32_t* nb = &out_neighbours[i * (size_t)PCA_ORIENT_NEIGHBOURS];
				for (size_t t = 0; t < keep; ++t) { nb[t] = cand[t].second; }

				//Covariance about the neighbourhood's own centroid, not about the point
				//being fitted - a point sitting off the surface would otherwise tilt its
				//own plane toward itself. The point is included in the set, which is what
				//keeps a fit on a sharply curved patch anchored where it is being asked
				//about.
				double mx = p.x, my = p.y, mz = p.z;
				for (size_t t = 0; t < take; ++t) {
					const float3& q = pts[cand[t].second].position;
					mx += q.x; my += q.y; mz += q.z;
				}
				const double inv = 1.0 / (double)(take + 1);
				mx *= inv; my *= inv; mz *= inv;

				double cxx = 0.0, cyy = 0.0, czz = 0.0, cxy = 0.0, cxz = 0.0, cyz = 0.0;
				auto accumulate = [&](const float3& q) {
					const double dx = (double)q.x - mx;
					const double dy = (double)q.y - my;
					const double dz = (double)q.z - mz;
					cxx += dx * dx; cyy += dy * dy; czz += dz * dz;
					cxy += dx * dy; cxz += dx * dz; cyz += dy * dz;
				};
				accumulate(p);
				for (size_t t = 0; t < take; ++t) {
					accumulate(pts[cand[t].second].position);
				}

				//Left unnormalized by the point count: an eigenvector does not change
				//under a uniform scale of the matrix.
				float m[3][3] = {
					{ (float)cxx, (float)cxy, (float)cxz },
					{ (float)cxy, (float)cyy, (float)cyz },
					{ (float)cxz, (float)cyz, (float)czz }
				};
				out_axis[i] = SmallestEigenvector(m);
			}
		};

		unsigned int threads = std::thread::hardware_concurrency();
		if (threads == 0) { threads = 1; }
		if (pts.size() < PCA_MIN_THREADED || threads <= 1) {
			worker(0, pts.size());
			return;
		}

		//Every thread reads the grid and writes a disjoint span of out_axis, so there is
		//nothing to lock. Load runs off the render thread already (SplatCloudData::Prepare
		//is the half that must not), so this is free to take the machine.
		std::vector<std::thread> pool;
		pool.reserve(threads);
		const size_t chunk = (pts.size() + threads - 1) / threads;
		for (unsigned int t = 0; t < threads; ++t) {
			const size_t first = (size_t)t * chunk;
			if (first >= pts.size()) { break; }
			const size_t last = ((first + chunk) < pts.size()) ? (first + chunk) : pts.size();
			pool.emplace_back(worker, first, last);
		}
		for (std::thread& th : pool) { th.join(); }
	}

	//A canonical sign for a direction that nothing else orients. Used only where the
	//centroid rule has no signal at all, and its one job is to be *consistent*: two
	//nearly parallel normals must come out the same way round. It is not "correct",
	//because nothing local can be.
	float3 OrientTowards(const float3& n, const float3& reference) {
		if (n.x * reference.x + n.y * reference.y + n.z * reference.z < 0.0f) {
			return float3{ -n.x, -n.y, -n.z };
		}
		return n;
	}

	//--- Making the signs agree ---------------------------------------------------
	//
	// A fitted plane gives an AXIS, not a direction: the eigenvector is as valid
	// negated, and the fit has no opinion at all about which side of the surface is
	// out. Deciding that per point - "point it away from the cloud centroid", which is
	// what a Gaussian's minor axis gets - is wrong in a way that is worse than being
	// upside down, because neighbouring points decide *independently*. On anything that
	// is not a single convex blob seen from outside (a scanned scene: several objects,
	// concavities, a floor running through the middle of the cloud) whole regions come
	// out inverted against their neighbours, and the joins between them are hard edges
	// in the normal buffer - patches of the opposite colour sitting inside a smooth
	// gradient. No amount of improving the *fit* touches it; the fit was already right.
	//
	// So the sign is propagated along the neighbour graph instead, which is Hoppe et
	// al.'s construction (Surface Reconstruction from Unorganized Points, 1992) and
	// still what PCL and Open3D do: grow a minimum spanning tree over the k-NN graph
	// with each edge weighted 1 - |dot(ni, nj)|, and flip each point as it is reached to
	// agree with the neighbour that reached it. The weight is what makes it an MST
	// rather than a flood fill, and it is the whole trick: the cheapest edges are the
	// ones between nearly parallel normals, so the traversal crosses flat ground first
	// and only steps over a crease when it has no other way in - by which point both
	// sides of the crease are already settled and cannot drag a region with them.
	//
	// That fixes agreement, not absolute direction: a component can still come out
	// inside-out as a whole, which is one flip rather than thousands and is what
	// SplatCloud::invert_normals is for.

	//Orients axes in place. `neighbours` is the graph from EstimatePointCloudNormals.
	void OrientNormalsConsistently(const std::vector<SplatVertex>& pts,
		const std::vector<uint32_t>& neighbours, std::vector<float3>& axes) {
		const size_t n = pts.size();
		if (n == 0) { return; }

		std::vector<uint8_t> visited(n, 0);
		//Lazy Prim: `best_w` is the cheapest edge found into a node so far, and an entry
		//is only pushed when it improves on that. Without it every edge of the graph is
		//queued (8n of them) and the queue, not the traversal, is the cost of the pass.
		std::vector<float> best_w(n, FLT_MAX);
		std::vector<uint32_t> best_from(n, UINT32_MAX);

		using Entry = std::pair<float, uint32_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

		//Reused across components rather than allocated per component - a scanned scene
		//is one component per object and there can be thousands of them.
		std::vector<uint32_t> component;

		for (size_t seed = 0; seed < n; ++seed) {
			if (visited[seed]) { continue; }

			//A new connected component. The graph is not connected in general and that is
			//not a defect: two objects a metre apart genuinely share no neighbour, and
			//nothing about one of them implies which way round the other should be. Each
			//gets its own traversal and its own global sign below.
			component.clear();
			best_w[seed] = 0.0f;
			best_from[seed] = UINT32_MAX;
			pq.push(Entry(0.0f, (uint32_t)seed));

			while (!pq.empty()) {
				const Entry e = pq.top();
				pq.pop();
				const uint32_t i = e.second;
				if (visited[i]) { continue; }
				visited[i] = 1;
				component.push_back(i);

				//Agree with whoever reached us. The parent is always already visited and
				//therefore already final, so this never has to be revisited.
				if (best_from[i] != UINT32_MAX) {
					const float3& parent = axes[best_from[i]];
					float3& ni = axes[i];
					if (ni.x * parent.x + ni.y * parent.y + ni.z * parent.z < 0.0f) {
						ni.x = -ni.x; ni.y = -ni.y; ni.z = -ni.z;
					}
				}

				const uint32_t* nb = &neighbours[(size_t)i * (size_t)PCA_ORIENT_NEIGHBOURS];
				for (uint32_t t = 0; t < PCA_ORIENT_NEIGHBOURS; ++t) {
					const uint32_t j = nb[t];
					if (j == UINT32_MAX || visited[j]) { continue; }
					//|dot|, so the weight does not depend on signs that have not been
					//decided yet - which is what lets the tree be built and the flipping
					//be done in the same walk.
					const float d = fabsf(axes[i].x * axes[j].x + axes[i].y * axes[j].y + axes[i].z * axes[j].z);
					const float w = 1.0f - d;
					if (w < best_w[j]) {
						best_w[j] = w;
						best_from[j] = i;
						pq.push(Entry(w, j));
					}
				}
			}

			//The component now agrees with itself and may still be inside-out as a whole.
			//Point it away from its OWN centroid, not the cloud's: in a scene the cloud
			//centroid sits in the air between the objects and says nothing useful about
			//any of them.
			double ccx = 0.0, ccy = 0.0, ccz = 0.0;
			for (const uint32_t i : component) {
				ccx += pts[i].position.x; ccy += pts[i].position.y; ccz += pts[i].position.z;
			}
			const double inv = 1.0 / (double)component.size();
			const float3 cc = { (float)(ccx * inv), (float)(ccy * inv), (float)(ccz * inv) };

			//Averaged as a cosine so the test is against NORMAL_CENTROID_MIN_COS on the
			//same footing a single point would be: a sum of raw dot products is dominated
			//by whichever points happen to sit furthest out.
			double mean_cos = 0.0;
			uint32_t highest = component[0];
			for (const uint32_t i : component) {
				const float3& p = pts[i].position;
				const float ox = p.x - cc.x, oy = p.y - cc.y, oz = p.z - cc.z;
				const float ol = sqrtf(ox * ox + oy * oy + oz * oz);
				if (ol > 1e-8f) {
					mean_cos += (double)((axes[i].x * ox + axes[i].y * oy + axes[i].z * oz) / ol);
				}
				if (p.y > pts[highest].position.y) { highest = i; }
			}
			mean_cos *= inv;

			bool flip;
			if (fabs(mean_cos) > (double)NORMAL_CENTROID_MIN_COS) {
				flip = (mean_cos < 0.0);
			}
			else {
				//A flat component - a floor, a wall, a single scanned facade - where the
				//outward direction lies in the surface and the centroid rule means
				//nothing. Hoppe's own tie-break: the topmost point of a surface faces up.
				flip = (axes[highest].y < 0.0f);
			}
			if (flip) {
				for (const uint32_t i : component) {
					axes[i].x = -axes[i].x; axes[i].y = -axes[i].y; axes[i].z = -axes[i].z;
				}
			}
		}
	}
}

SplatCloudData::~SplatCloudData() {
	Unprepare();
}

bool SplatCloudData::Load(const std::string& file, const std::string& asset_name) {
	std::ifstream in(file, std::ios::binary);
	if (!in.is_open()) {
		LOG_ERROR("SplatCloudData::Load: cannot open %s", file.c_str());
		return false;
	}

	std::string line;
	if (!std::getline(in, line) || line.rfind("ply", 0) != 0) {
		LOG_ERROR("SplatCloudData::Load: %s is not a .ply", file.c_str());
		return false;
	}

	bool binary_le = false;
	uint64_t vertex_count = 0;
	std::vector<PlyProperty> props;
	//Properties are only counted for the *vertex* element. A .ply may declare a face
	//element after it, whose properties are lists and must not be added to the
	//per-vertex stride.
	bool in_vertex_element = false;

	while (std::getline(in, line)) {
		//Headers are ASCII but the file may have been written on a platform that used
		//CRLF, and the trailing \r would otherwise end up inside a property name.
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		std::istringstream ss(line);
		std::string tok;
		ss >> tok;

		if (tok == "format") {
			std::string fmt;
			ss >> fmt;
			if (fmt == "binary_little_endian") {
				binary_le = true;
			}
			else {
				LOG_ERROR("SplatCloudData::Load: %s is '%s'; only binary_little_endian is supported", file.c_str(), fmt.c_str());
				return false;
			}
		}
		else if (tok == "element") {
			std::string ename;
			ss >> ename;
			in_vertex_element = (ename == "vertex");
			if (in_vertex_element) {
				ss >> vertex_count;
			}
		}
		else if (tok == "property" && in_vertex_element) {
			std::string type;
			ss >> type;
			if (type == "list") {
				LOG_ERROR("SplatCloudData::Load: %s has a list property on the vertex element", file.c_str());
				return false;
			}
			PlyProperty p;
			p.size = PlyTypeSize(type, p.is_float, p.is_double);
			ss >> p.name;
			if (p.size == 0) {
				LOG_ERROR("SplatCloudData::Load: unknown property type '%s' in %s", type.c_str(), file.c_str());
				return false;
			}
			props.push_back(p);
		}
		else if (tok == "end_header") {
			break;
		}
	}

	if (!binary_le || vertex_count == 0 || props.empty()) {
		LOG_ERROR("SplatCloudData::Load: %s has no usable vertex element", file.c_str());
		return false;
	}

	//Index the properties by name once, so the per-splat loop is offset arithmetic
	//rather than a string compare per field per splat.
	std::unordered_map<std::string, uint32_t> offset_of;
	std::unordered_map<std::string, const PlyProperty*> prop_of;
	uint32_t stride = 0;
	for (const auto& p : props) {
		offset_of[p.name] = stride;
		prop_of[p.name] = &p;
		stride += p.size;
	}

	auto has = [&](const char* n) { return offset_of.find(n) != offset_of.end(); };

	//x/y/z are the one thing every splat needs no matter where the rest of its
	//parameters come from.
	const char* position_required[] = { "x", "y", "z" };
	for (const char* r : position_required) {
		if (!has(r)) {
			LOG_ERROR("SplatCloudData::Load: %s is missing required property '%s'", file.c_str(), r);
			return false;
		}
	}

	//A trained 3DGS splat. Normals are deliberately not required - see the header.
	const char* gaussian_required[] = { "opacity",
										"scale_0", "scale_1", "scale_2",
										"rot_0", "rot_1", "rot_2", "rot_3",
										"f_dc_0", "f_dc_1", "f_dc_2" };
	bool is_gaussian = true;
	for (const char* r : gaussian_required) {
		if (!has(r)) { is_gaussian = false; break; }
	}

	//Not every .ply carries Gaussian parameters - a plain coloured point cloud (the
	//shape a 3D scanner like Artec or a photogrammetry tool exports) has only a
	//position and a colour per vertex. That is still worth drawing as a splat cloud:
	//below, a missing scale/rotation/opacity falls back to an isotropic, opaque,
	//constant-radius blob per point rather than failing the load outright.
	const bool has_color = has("red") && has("green") && has("blue");
	if (!is_gaussian && !has_color) {
		LOG_ERROR("SplatCloudData::Load: %s has no Gaussian splat properties (e.g. 'opacity') "
			"and no red/green/blue vertex colour to fall back to a plain point cloud; "
			"is it a 3DGS .ply?", file.c_str());
		return false;
	}

	std::vector<uint8_t> raw(stride * (size_t)vertex_count);
	in.read((char*)raw.data(), (std::streamsize)raw.size());
	if ((uint64_t)in.gcount() != (uint64_t)raw.size()) {
		LOG_ERROR("SplatCloudData::Load: %s is truncated (wanted %llu bytes, got %lld)",
			file.c_str(), (unsigned long long)raw.size(), (long long)in.gcount());
		return false;
	}
	in.close();

	//Reads one scalar property as a float, widening whatever the file stored it as.
	auto read_f = [&](const uint8_t* base, const char* name) -> float {
		auto it = offset_of.find(name);
		if (it == offset_of.end()) { return 0.0f; }
		const PlyProperty* p = prop_of[name];
		const uint8_t* at = base + it->second;
		if (p->is_float) { float v; memcpy(&v, at, 4); return v; }
		if (p->is_double) { double v; memcpy(&v, at, 8); return (float)v; }
		//Integer-typed. Only ever seen on nx/ny/nz in odd exports, and those are
		//discarded anyway, so a plain widening is enough.
		switch (p->size) {
		case 1: return (float)*at;
		case 2: { uint16_t v; memcpy(&v, at, 2); return (float)v; }
		case 4: { uint32_t v; memcpy(&v, at, 4); return (float)v; }
		default: return 0.0f;
		}
	};

	//Reads one colour channel normalized to [0,1], scaling by whatever integer width
	//the file stored it as (uchar is what every scanner/photogrammetry exporter uses);
	//a float/double channel is assumed already normalized.
	auto read_color = [&](const uint8_t* base, const char* name) -> float {
		auto it = offset_of.find(name);
		if (it == offset_of.end()) { return 1.0f; }
		const PlyProperty* p = prop_of[name];
		const uint8_t* at = base + it->second;
		if (p->is_float) { float v; memcpy(&v, at, 4); return v; }
		if (p->is_double) { double v; memcpy(&v, at, 8); return (float)v; }
		switch (p->size) {
		case 1: return (float)*at / 255.0f;
		case 2: { uint16_t v; memcpy(&v, at, 2); return (float)v / 65535.0f; }
		case 4: { uint32_t v; memcpy(&v, at, 4); return (float)v / 4294967295.0f; }
		default: return 0.0f;
		}
	};

	splats.clear();
	splats.reserve((size_t)vertex_count);

	//First pass builds the splats and accumulates the centroid, which the normal's
	//sign resolution needs and which is therefore not known until every centre is read.
	double cx = 0.0, cy = 0.0, cz = 0.0;
	//The un-oriented normal per splat, filled alongside so the second pass only has to
	//decide a sign.
	std::vector<float3> minor_axis;
	minor_axis.reserve((size_t)vertex_count);

	for (uint64_t i = 0; i < vertex_count; ++i) {
		const uint8_t* base = raw.data() + (size_t)i * stride;

		SplatVertex s;
		s.position = { read_f(base, "x"), read_f(base, "y"), read_f(base, "z") };

		//The file stores log-scales; exponentiate to get world units. A point cloud
		//has no scale_0-2 of its own, and read_f already answers a missing property
		//with 0 - so this falls back to expf(0) = 1 world unit with no special case
		//needed. That is deliberately not tuned to the cloud's size: guessing a
		//"denser" radius from the point count and extent was tried and produced
		//wildly wrong results on real captures, whereas a fixed, predictable
		//baseline is one the component's point_size_scale can be dialled against
		//(Components::SplatCloud, Tools/SceneEditor/Inspector.cpp's "Point size").
		//Opacity is likewise forced to full for a point cloud - it has no notion of
		//a soft, translucent Gaussian, and leaving it at the missing-field default
		//(Sigmoid(0) = 0.5) would render every point half-see-through instead of as
		//a solid surface.
		if (!is_gaussian) {
			s.opacity = 1.0f;
		}
		else {
			s.opacity = Sigmoid(read_f(base, "opacity"));
		}
		const float sx = expf(read_f(base, "scale_0"));
		const float sy = expf(read_f(base, "scale_1"));
		const float sz = expf(read_f(base, "scale_2"));

		//3DGS stores the rotation as (w, x, y, z) and does not guarantee it normalized.
		float qw = read_f(base, "rot_0");
		float qx = read_f(base, "rot_1");
		float qy = read_f(base, "rot_2");
		float qz = read_f(base, "rot_3");
		float qlen = sqrtf(qw * qw + qx * qx + qy * qy + qz * qz);
		if (qlen > 1e-8f) { qw /= qlen; qx /= qlen; qy /= qlen; qz /= qlen; }
		else { qw = 1.0f; qx = qy = qz = 0.0f; }

		//Rotation matrix, column-major as three basis vectors: R = [r0 r1 r2]. Each
		//column is the world direction of one of the ellipsoid's local axes, which is
		//exactly what the normal derivation below needs.
		const float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
		const float r01 = 2.0f * (qx * qy - qw * qz);
		const float r02 = 2.0f * (qx * qz + qw * qy);
		const float r10 = 2.0f * (qx * qy + qw * qz);
		const float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
		const float r12 = 2.0f * (qy * qz - qw * qx);
		const float r20 = 2.0f * (qx * qz - qw * qy);
		const float r21 = 2.0f * (qy * qz + qw * qx);
		const float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);

		//Sigma = R * S * S^T * R^T with S diagonal, which reduces to summing the outer
		//product of each scaled basis column. Only the upper triangle is kept - the
		//matrix is symmetric by construction.
		const float ax = sx * sx, ay = sy * sy, az = sz * sz;
		s.cov_diag.x = r00 * r00 * ax + r01 * r01 * ay + r02 * r02 * az;
		s.cov_diag.y = r10 * r10 * ax + r11 * r11 * ay + r12 * r12 * az;
		s.cov_diag.z = r20 * r20 * ax + r21 * r21 * ay + r22 * r22 * az;
		s.cov_offdiag.x = r00 * r10 * ax + r01 * r11 * ay + r02 * r12 * az;  //Sxy
		s.cov_offdiag.y = r00 * r20 * ax + r01 * r21 * ay + r02 * r22 * az;  //Sxz
		s.cov_offdiag.z = r10 * r20 * ax + r11 * r21 * ay + r12 * r22 * az;  //Syz

		//Into the engine's frame. A 3DGS .ply comes out of the COLMAP/OpenCV convention
		//the reference trainer works in: right-handed, X right, **Y down**, Z forward.
		//This engine is left-handed Y-up (XMMatrixPerspectiveFovLH / XMMatrixLookToLH
		//throughout), so negating Y converts the axis and the handedness in one step.
		//
		//Loaded raw - which is what happened before - a capture renders upside down AND
		//mirrored, and the two together read as "the mesh is inverted" rather than as a
		//convention error. Rotating the entity 180 degrees is not the same fix: it
		//corrects the axis and leaves the handedness flipped, so the model stays
		//mirrored.
		//
		//This is a change of basis by D = diag(1,-1,1), so the covariance follows as
		//Sigma' = D * Sigma * D: the diagonal is untouched (D squares to the identity on
		//it) and the two off-diagonal terms carrying exactly one Y index flip sign. Sxz
		//carries none and does not. Applying it here rather than to the quaternion keeps
		//the sign rule next to the matrix it acts on, and avoids reasoning about what a
		//handedness flip does to a rotation.
		s.position.y = -s.position.y;
		s.cov_offdiag.x = -s.cov_offdiag.x;  //Sxy
		s.cov_offdiag.z = -s.cov_offdiag.z;  //Syz

		if (is_gaussian) {
			//SH degree 0 -> colour, then kept as albedo. Clamped at zero because a
			//negative coefficient is legal in the fit but a negative albedo is not, and
			//it would drive the lighting negative rather than merely looking wrong.
			s.albedo.x = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_0"));
			s.albedo.y = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_1"));
			s.albedo.z = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_2"));
		}
		else {
			//No SH coefficients to invert - the file's own vertex colour is the albedo.
			s.albedo.x = read_color(base, "red");
			s.albedo.y = read_color(base, "green");
			s.albedo.z = read_color(base, "blue");
		}

		//A capture carries no specular information at all - a radiance field folds
		//every highlight into the colour it fitted. So this is a constant the material
		//supplies, not something recovered from the file, and it starts low because a
		//scanned surface that is already carrying its own baked highlights should not
		//get a second one on top.
		s.spec_intensity = 0.1f;

		//The minor axis: the basis column belonging to the smallest scale. A trained
		//Gaussian is flat against the surface it represents, so its shortest axis is
		//the surface normal.
		//
		//Only a trained Gaussian, though. A point cloud has no scale_* or rot_* at all,
		//so sx == sy == sz and R is the identity - the ellipsoid is a sphere, which has
		//no minor axis, and this derivation would hand every point in the file the same
		//basis column. Those clouds are filled in below instead, from the neighbouring
		//points, which is the only place the information exists.
		if (is_gaussian) {
			float3 axis;
			if (sx <= sy && sx <= sz) { axis = { r00, r10, r20 }; }
			else if (sy <= sx && sy <= sz) { axis = { r01, r11, r21 }; }
			else { axis = { r02, r12, r22 }; }
			//Still in the file's frame - it is read off R, which was built before the basis
			//change above - so it takes the same Y flip. Missed, the normals would disagree
			//with the geometry by a mirror and the cloud would light as though the sun were
			//on the other side of it.
			axis.y = -axis.y;
			minor_axis.push_back(axis);
		}

		cx += s.position.x;
		cy += s.position.y;
		cz += s.position.z;

		splats.push_back(s);
	}

	const float3 centroid = {
		(float)(cx / (double)vertex_count),
		(float)(cy / (double)vertex_count),
		(float)(cz / (double)vertex_count)
	};

	//A point cloud's normals come from the neighbourhood rather than from the point, in
	//two steps: fit the plane, then make the signs agree along the neighbour graph. The
	//second is not a refinement of the first - the fit is already right without it - it
	//is what stops neighbouring points choosing opposite sides of the same surface. Both
	//run on the positions as they now stand, already through the Y flip above, so what
	//comes out is in engine space and takes no second flip. A Gaussian takes the first
	//step too (see GAUSSIAN_NORMAL_WEIGHT), blending in its own per-splat axis rather
	//than trusting the fit alone - but standard 3DGS training gives that per-splat axis
	//no guarantee of agreeing with its neighbours either, so it still needs the second
	//step: on anything that is not a single convex blob (a limb, a strap, the inside of
	//a helmet - anywhere "away from the whole cloud's centroid" is the wrong side for
	//that one patch) the centroid-only fallback flips isolated splats against their
	//surroundings, which is what a scattered, high-frequency salt-and-pepper pattern in
	//the normal buffer looks like. The graph propagation runs on the blended axis
	//instead of replacing it, so a trained splat still keeps its own signal - it is only
	//the sign that neighbours are now made to agree on.
	bool normals_oriented = false;
	{
		const auto started = std::chrono::steady_clock::now();
		//Written into its own vector rather than minor_axis directly: for a Gaussian,
		//minor_axis already holds the trained per-splat axis (pushed above) and this
		//fit is only going in to be blended with it, not to replace it.
		std::vector<float3> fitted_axis;
		std::vector<uint32_t> neighbours;
		EstimatePointCloudNormals(splats, fitted_axis, neighbours);
		const double fit_ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - started).count();

		if (is_gaussian) {
			//The fit's sign is ambiguous per splat - it comes from an eigenvector, not
			//a measurement - so there is nothing to propagate it against here the way
			//OrientNormalsConsistently does for a point cloud. Instead each fitted axis
			//is flipped to agree with that same splat's own trained axis before the
			//blend, which is a real reference (if a noisy one) precisely because it is
			//per splat rather than derived from the neighbourhood.
			for (size_t i = 0; i < minor_axis.size(); ++i) {
				float3& trained = minor_axis[i];
				float3 fitted = fitted_axis[i];
				const float d = trained.x * fitted.x + trained.y * fitted.y + trained.z * fitted.z;
				if (d < 0.0f) {
					fitted.x = -fitted.x; fitted.y = -fitted.y; fitted.z = -fitted.z;
				}
				trained.x = GAUSSIAN_NORMAL_WEIGHT * trained.x + (1.0f - GAUSSIAN_NORMAL_WEIGHT) * fitted.x;
				trained.y = GAUSSIAN_NORMAL_WEIGHT * trained.y + (1.0f - GAUSSIAN_NORMAL_WEIGHT) * fitted.y;
				trained.z = GAUSSIAN_NORMAL_WEIGHT * trained.z + (1.0f - GAUSSIAN_NORMAL_WEIGHT) * fitted.z;
			}

			//Blending only fixes each splat's sign against its OWN fit; nothing above
			//makes one splat agree with the next, which is exactly what the centroid-only
			//pass below cannot do on a non-convex shape (see the comment above this
			//block). Run the same neighbour-graph propagation a plain point cloud relies
			//on entirely, on top of the blended axis - normals_oriented then skips the
			//centroid pass below for the same reason it does for a point cloud: it would
			//undo the agreement this just produced.
			const auto oriented_at = std::chrono::steady_clock::now();
			OrientNormalsConsistently(splats, neighbours, minor_axis);
			const double orient_ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - oriented_at).count();
			normals_oriented = true;

			LOG_INFO("SplatCloudData::Load: blended %llu gaussian normals with a neighbourhood fit "
				"(%.0f%% trained) in %.0f ms, oriented along a %u-neighbour graph in %.0f ms",
				(unsigned long long)splats.size(), GAUSSIAN_NORMAL_WEIGHT * 100.0f, fit_ms,
				(unsigned)PCA_ORIENT_NEIGHBOURS, orient_ms);
		}
		else {
			minor_axis = std::move(fitted_axis);

			const auto oriented_at = std::chrono::steady_clock::now();
			OrientNormalsConsistently(splats, neighbours, minor_axis);
			const double orient_ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - oriented_at).count();

			normals_oriented = true;
			LOG_INFO("SplatCloudData::Load: fitted %llu point-cloud normals from %u neighbours in %.0f ms, "
				"oriented along a %u-neighbour graph in %.0f ms",
				(unsigned long long)splats.size(), (unsigned)PCA_NEIGHBOURS, fit_ms,
				(unsigned)PCA_ORIENT_NEIGHBOURS, orient_ms);
		}
	}

	//Second pass: orient the normals and measure the bounds.
	//
	//An ellipsoid's minor axis has no sign - it is an axis, not a direction - and the
	//file gives nothing to disambiguate it. Pointing it away from the cloud's centroid
	//is right for a scan of an object viewed from outside, which is the common case,
	//and wrong for a room scanned from within, where every normal comes out facing the
	//wall. There is no way to tell the two apart from the point set alone, which is why
	//the component carries invert_normals.
	float3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
	float3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	//Where the centroid gives no signal at all (below), the sign is resolved against
	//this instead - the first splat's own axis, whichever way round it happens to be.
	//An arbitrary reference is the point: it makes a flat cloud, which is the case that
	//degenerates, come out agreeing with itself rather than dithering.
	float3 degenerate_ref = { 0.0f, 1.0f, 0.0f };
	for (const float3& a : minor_axis) {
		const float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
		if (l > 1e-8f) { degenerate_ref = { a.x / l, a.y / l, a.z / l }; break; }
	}

	for (size_t i = 0; i < splats.size(); ++i) {
		SplatVertex& s = splats[i];
		float3 n = minor_axis[i];
		const float nlen = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
		if (nlen > 1e-8f) { n.x /= nlen; n.y /= nlen; n.z /= nlen; }
		else { n = { 0.0f, 1.0f, 0.0f }; }

		//A point cloud's normals are already oriented, per connected component, by
		//something that looked at far more than this one point. Re-running the centroid
		//rule over the top would undo exactly the agreement that pass exists to produce.
		if (!normals_oriented) {
			const float3 out = { s.position.x - centroid.x,
								 s.position.y - centroid.y,
								 s.position.z - centroid.z };
			const float outlen = sqrtf(out.x * out.x + out.y * out.y + out.z * out.z);
			const float d = n.x * out.x + n.y * out.y + n.z * out.z;
			//The centroid rule only says anything where the normal has a real component
			//along the outward direction. On a flat cloud the outward direction lies *in*
			//the surface, so d is whatever the fit's own error made it and the sign comes
			//out as per-point noise: neighbouring splats face opposite ways and the cloud
			//renders as static rather than as a surface. Falling back to a fixed reference
			//is not more correct - nothing local is - but it is consistent, which is the
			//difference between a cloud lit the wrong way round (invert_normals fixes
			//that) and a cloud that cannot be lit at all.
			if (fabsf(d) > NORMAL_CENTROID_MIN_COS * outlen) {
				if (d < 0.0f) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }
			}
			else {
				n = OrientTowards(n, degenerate_ref);
			}
		}
		s.normal = n;

		//3 sigma along each world axis. The covariance diagonal is variance, so the
		//standard deviation is its square root - taking the diagonal directly here
		//would under-measure the box by squaring an already-small number.
		const float ex = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.x));
		const float ey = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.y));
		const float ez = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.z));

		bmin.x = fminf(bmin.x, s.position.x - ex);
		bmin.y = fminf(bmin.y, s.position.y - ey);
		bmin.z = fminf(bmin.z, s.position.z - ez);
		bmax.x = fmaxf(bmax.x, s.position.x + ex);
		bmax.y = fmaxf(bmax.y, s.position.y + ey);
		bmax.z = fmaxf(bmax.z, s.position.z + ez);
	}

	min_dimensions = bmin;
	max_dimensions = bmax;
	splat_count = (uint32_t)splats.size();
	name = asset_name;
	source_file = file;

	LOG_INFO("SplatCloudData::Load: %s -> '%s', %llu %s splats, box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
		file.c_str(), asset_name.c_str(), (unsigned long long)splats.size(),
		is_gaussian ? "gaussian" : "point cloud",
		bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);

	return true;
}

void SplatCloudData::BuildDefault(const std::string& asset_name) {
	//A Fibonacci sphere: the cheapest way to get points that are near-uniform over a
	//sphere without the pole clustering a lat/long grid produces, which would show up
	//as two bright spots on the stand-in.
	constexpr uint32_t COUNT = 2048;
	constexpr float RADIUS = 0.5f;
	//Tangential extent of one splat. Sized so the sphere reads as a surface rather
	//than as separate dots: roughly the spacing between neighbours at this count.
	constexpr float TANGENT_SIGMA = 0.030f;
	//Radial extent, deliberately much smaller - this is what makes each splat a disc
	//lying on the sphere, and therefore what makes its minor axis the surface normal.
	constexpr float RADIAL_SIGMA = 0.006f;

	//The golden angle, pi * (3 - sqrt(5)). Spelled out rather than reached through
	//M_PI, which needs _USE_MATH_DEFINES before <cmath> and is not defined here.
	constexpr float golden = 2.39996322972865332f;

	splats.clear();
	splats.reserve(COUNT);

	float3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
	float3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (uint32_t i = 0; i < COUNT; ++i) {
		const float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)COUNT;
		const float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
		const float theta = golden * (float)i;
		const float3 n = { cosf(theta) * r, y, sinf(theta) * r };

		SplatVertex s;
		s.position = { n.x * RADIUS, n.y * RADIUS, n.z * RADIUS };
		s.normal = n;
		s.opacity = 1.0f;
		s.spec_intensity = 0.1f;
		//Normal-as-colour, remapped from [-1,1]. Makes the orientation of the stand-in
		//readable at a glance, which is the whole job of a stand-in.
		s.albedo = { n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f };

		//Sigma = R S S^T R^T with the local frame's third axis along the normal. Built
		//directly rather than through a quaternion: only the outer products are needed,
		//and an anisotropic diagonal in a frame whose third axis is n is exactly
		//(t1 t1^T + t2 t2^T) * tangent^2 + n n^T * radial^2. Because the tangential
		//variances are equal, the two tangent vectors cancel out of the sum and the
		//whole thing reduces to an isotropic term minus the radial deficit along n -
		//so no tangent basis has to be constructed at all.
		const float ta = TANGENT_SIGMA * TANGENT_SIGMA;
		const float ra = RADIAL_SIGMA * RADIAL_SIGMA;
		const float d = ra - ta;
		s.cov_diag = { ta + d * n.x * n.x, ta + d * n.y * n.y, ta + d * n.z * n.z };
		s.cov_offdiag = { d * n.x * n.y, d * n.x * n.z, d * n.y * n.z };

		const float ex = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.x));
		const float ey = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.y));
		const float ez = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.z));
		bmin.x = fminf(bmin.x, s.position.x - ex);
		bmin.y = fminf(bmin.y, s.position.y - ey);
		bmin.z = fminf(bmin.z, s.position.z - ez);
		bmax.x = fmaxf(bmax.x, s.position.x + ex);
		bmax.y = fmaxf(bmax.y, s.position.y + ey);
		bmax.z = fmaxf(bmax.z, s.position.z + ez);

		splats.push_back(s);
	}

	min_dimensions = bmin;
	max_dimensions = bmax;
	splat_count = (uint32_t)splats.size();
	name = asset_name;
	source_file.clear();
}

HRESULT SplatCloudData::Prepare() {
	if (prepared || splats.empty()) {
		return S_OK;
	}
	ID3D11Device* device = Core::DXCore::Get()->device;

	D3D11_BUFFER_DESC bd{};
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.ByteWidth = (UINT)(sizeof(SplatVertex) * splats.size());
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(SplatVertex);

	D3D11_SUBRESOURCE_DATA init{};
	init.pSysMem = splats.data();

	HRESULT hr = device->CreateBuffer(&bd, &init, &buffer);
	if (FAILED(hr)) {
		LOG_ERROR("SplatCloudData::Prepare: CreateBuffer failed for '%s' (%llu splats, %.1f MB, 0x%08x)",
			name.c_str(), (unsigned long long)splats.size(),
			(double)bd.ByteWidth / (1024.0 * 1024.0), hr);
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
	srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = (uint32_t)splats.size();
	hr = device->CreateShaderResourceView(buffer, &srv_desc, &srv);
	if (FAILED(hr)) {
		LOG_ERROR("SplatCloudData::Prepare: CreateShaderResourceView failed for '%s' (0x%08x)",
			name.c_str(), hr);
		buffer->Release();
		buffer = nullptr;
		return hr;
	}

	prepared = true;
	LOG_INFO("SplatCloudData::Prepare: '%s' uploaded, %llu splats, %.1f MB",
		name.c_str(), (unsigned long long)splats.size(),
		(double)bd.ByteWidth / (1024.0 * 1024.0));
	return S_OK;
}

void SplatCloudData::ReleaseCpuCopy() {
	//swap-with-empty, because clear() on a vector keeps the capacity - which for a
	//capture is the entire allocation this is trying to give back.
	std::vector<SplatVertex>().swap(splats);
}

void SplatCloudData::Unprepare() {
	if (srv != nullptr) {
		srv->Release();
		srv = nullptr;
	}
	if (buffer != nullptr) {
		buffer->Release();
		buffer = nullptr;
	}
	prepared = false;
}
