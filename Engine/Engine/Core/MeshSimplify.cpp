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

#include <Core/MeshSimplify.h>
#include <Core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

namespace {

	//How much heavier an open boundary is than the surface it bounds. The border
	//planes below are area weighted like every other plane in the quadric, so this
	//is a plain multiplier and needs no scaling to the model: at 100 an edge of the
	//silhouette costs two orders of magnitude more than an interior edge of the same
	//size, which is enough to make the boundary the last thing a reduction touches
	//without locking it (a locked boundary stops a shell - a leaf, a cape, a piece of
	//terrain - from reducing at all, since every one of its vertices is on it).
	constexpr double BORDER_WEIGHT = 100.0;

	//The same idea for texture seams. A seam vertex has two sets of UVs sitting on
	//one position, and collapsing it means the triangles on one side have to adopt
	//coordinates from the other island - which stretches the texture across a cut
	//that was put there precisely because the two sides are far apart in the map. It
	//is a penalty rather than a lock for the same reason as the border: on a model
	//whose UVs are cut into many islands, locking every seam leaves nothing to
	//collapse.
	constexpr double SEAM_PENALTY = 32.0;

	//Two UVs closer than this are the same coordinate. Well below a texel of any
	//sane map (1/4096 is 0.000244) and well above the noise of the importer's
	//float conversions.
	constexpr float UV_EPSILON = 1e-6f;

	//A collapse that turns a triangle over is refused outright rather than made
	//expensive: a folded triangle is not a worse approximation of the surface, it is
	//a hole with a piece of the surface hanging through it.
	constexpr double FLIP_COSINE = 0.0;

	//The symmetric 4x4 quadric of Garland-Heckbert, as its ten distinct entries:
	//  a2 ab ac ad  b2 bc bd  c2 cd  d2
	//It is the sum, over the planes meeting at a vertex, of the squared distance to
	//each - so evaluating it at a point says how far that point has drifted from the
	//surface the vertex used to describe.
	struct Quadric {
		double m[10] = {};

		void AddPlane(double a, double b, double c, double d, double w) {
			m[0] += w * a * a; m[1] += w * a * b; m[2] += w * a * c; m[3] += w * a * d;
			m[4] += w * b * b; m[5] += w * b * c; m[6] += w * b * d;
			m[7] += w * c * c; m[8] += w * c * d;
			m[9] += w * d * d;
		}
		void Add(const Quadric& o) {
			for (int i = 0; i < 10; ++i) {
				m[i] += o.m[i];
			}
		}
		double Eval(double x, double y, double z) const {
			return m[0] * x * x + 2.0 * m[1] * x * y + 2.0 * m[2] * x * z + 2.0 * m[3] * x
				+ m[4] * y * y + 2.0 * m[5] * y * z + 2.0 * m[6] * y
				+ m[7] * z * z + 2.0 * m[8] * z
				+ m[9];
		}
	};

	struct Vec3 {
		double x = 0.0, y = 0.0, z = 0.0;
	};

	Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vec3 Cross(const Vec3& a, const Vec3& b) {
		return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

	//Vertices are welded by exact position, and exactness is the point: the importer
	//clones a control point by copying it (FBXLoader::LoadMeshes), so every clone is
	//bit for bit the original and finds it here, while two control points the artist
	//placed a hair apart stay apart. A tolerance would weld those two - which is a
	//different operation, changes the model, and would make the result depend on a
	//threshold nobody chose.
	struct PosKey {
		uint32_t x = 0, y = 0, z = 0;
		bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; }
	};

	struct PosKeyHash {
		size_t operator()(const PosKey& k) const {
			//FNV-1a over the three words. A plain xor would give the same hash to any
			//two vertices whose coordinates are a permutation of each other, which on
			//a symmetric model is a great many of them.
			size_t h = 1469598103934665603ULL;
			const uint32_t words[3] = { k.x, k.y, k.z };
			for (uint32_t w : words) {
				h ^= (size_t)w;
				h *= 1099511628211ULL;
			}
			return h;
		}
	};

	PosKey MakeKey(const float3& p) {
		auto bits = [](float f) {
			//-0.0 and 0.0 are the same place and must hash alike.
			if (f == 0.0f) {
				f = 0.0f;
			}
			uint32_t u = 0;
			memcpy(&u, &f, sizeof(u));
			return u;
		};
		return PosKey{ bits(p.x), bits(p.y), bits(p.z) };
	}

	struct Triangle {
		//The welded (geometric) vertices this triangle spans, and the source vertex
		//each of its corners draws its attributes from. The first is what collapses;
		//the second is re-picked as the first moves, which is what keeps UVs, bone
		//weights and normals attached to geometry that survived.
		uint32_t g[3] = {};
		uint32_t c[3] = {};
		bool dead = false;
	};

	struct HeapItem {
		double cost = 0.0;
		uint32_t from = 0;  //the vertex that disappears
		uint32_t to = 0;    //the one it becomes
		//std::priority_queue is a max-heap; this makes it a min-heap. The tie-break
		//is what keeps the result reproducible - two collapses of equal cost have to
		//be ordered by something that does not depend on how the heap was built, or
		//the same mesh simplifies differently from one run to the next and a cached
		//result stops matching a freshly generated one.
		bool operator<(const HeapItem& o) const {
			if (cost != o.cost) {
				return cost > o.cost;
			}
			if (from != o.from) {
				return from > o.from;
			}
			return to > o.to;
		}
	};

	constexpr double COST_FORBIDDEN = std::numeric_limits<double>::infinity();

	class Simplifier {
	public:
		Simplifier(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
			: in_v(vertices), in_i(indices) {
		}

		//Welds the vertices, builds the triangle list and the per-vertex quadrics.
		//False when there is no surface here to reduce.
		bool Build();
		//Collapses until `target` vertices would be written out (or nothing legal is
		//left).
		void Run(size_t target);
		//Writes the surviving triangles out as a fresh vertex/index pair.
		void Emit(SimplifyResult& out) const;

		//How many vertices Emit would write right now. Counted rather than
		//estimated, and counted in *corners* rather than in welded positions,
		//because that is the number the result is asked for and the number the
		//renderer then selects on (MeshData::MeshLod::ratio is a share of
		//vertexCount). The two are not proportional: a reduction makes neighbouring
		//triangles adopt each other's corners, so the vertex count falls faster than
		//the triangle count - stopping on welded positions overshoots by a third on
		//a mesh whose corners all start out unshared, which every FBX import is.
		size_t LiveVertexCount() const { return live_corners; }

	private:
		double EvalCollapse(uint32_t from, uint32_t to) const;
		void Collapse(uint32_t from, uint32_t to);
		void PushEdgesAround(uint32_t v);
		void PushEdge(uint32_t a, uint32_t b);
		//The corner of `to` whose attributes are closest to `corner`, which is the
		//one the triangle should adopt when its own corner is collapsed away.
		uint32_t BestCorner(uint32_t to, uint32_t corner) const;
		std::vector<uint32_t> Neighbours(uint32_t v) const;
		//One reference per triangle slot pointing at a corner. A corner is written
		//out exactly while something still references it.
		void AcquireCorner(uint32_t c) {
			if (corner_refs[c]++ == 0) {
				live_corners++;
			}
		}
		void ReleaseCorner(uint32_t c) {
			if (--corner_refs[c] == 0) {
				live_corners--;
			}
		}
		bool SharesTriangle(const Triangle& t, uint32_t v) const {
			return t.g[0] == v || t.g[1] == v || t.g[2] == v;
		}

		const std::vector<Vertex>& in_v;
		const std::vector<uint32_t>& in_i;

		std::vector<Vec3> pos;                        //per welded vertex
		std::vector<std::vector<uint32_t>> corners;   //per welded vertex: source vertices there
		std::vector<Quadric> q;
		std::vector<std::vector<uint32_t>> vtris;     //per welded vertex: incident triangles
		std::vector<bool> dead;
		std::vector<bool> seam;
		std::vector<Triangle> tris;
		std::priority_queue<HeapItem> heap;
		std::vector<uint32_t> corner_refs;             //per source vertex
		size_t live = 0;                               //welded vertices still in use
		size_t live_corners = 0;                       //vertices Emit would write
	};

	bool Simplifier::Build() {
		std::unordered_map<PosKey, uint32_t, PosKeyHash> welded;
		welded.reserve(in_v.size());
		std::vector<uint32_t> vertex_group(in_v.size(), 0);
		for (size_t i = 0; i < in_v.size(); ++i) {
			const PosKey key = MakeKey(in_v[i].Position);
			auto it = welded.find(key);
			if (it == welded.end()) {
				const uint32_t id = (uint32_t)pos.size();
				welded.emplace(key, id);
				pos.push_back(Vec3{ in_v[i].Position.x, in_v[i].Position.y, in_v[i].Position.z });
				corners.push_back({ (uint32_t)i });
				vertex_group[i] = id;
			}
			else {
				corners[it->second].push_back((uint32_t)i);
				vertex_group[i] = it->second;
			}
		}
		q.resize(pos.size());
		vtris.resize(pos.size());
		dead.assign(pos.size(), false);
		seam.assign(pos.size(), false);
		corner_refs.assign(in_v.size(), 0);
		live = pos.size();

		for (size_t i = 0; i + 2 < in_i.size(); i += 3) {
			const uint32_t c0 = in_i[i], c1 = in_i[i + 1], c2 = in_i[i + 2];
			if (c0 >= in_v.size() || c1 >= in_v.size() || c2 >= in_v.size()) {
				continue;
			}
			Triangle t;
			t.c[0] = c0; t.c[1] = c1; t.c[2] = c2;
			t.g[0] = vertex_group[c0]; t.g[1] = vertex_group[c1]; t.g[2] = vertex_group[c2];
			//A triangle two of whose corners sit at one place has no area and no
			//plane; it contributes nothing and would only ever be in the way.
			if (t.g[0] == t.g[1] || t.g[1] == t.g[2] || t.g[0] == t.g[2]) {
				continue;
			}
			const uint32_t id = (uint32_t)tris.size();
			tris.push_back(t);
			for (uint32_t g : t.g) {
				vtris[g].push_back(id);
			}
			for (uint32_t corner : t.c) {
				AcquireCorner(corner);
			}
		}
		if (tris.empty()) {
			return false;
		}

		//Face quadrics, weighted by area: a big triangle says more about where the
		//surface is than a sliver does.
		std::vector<double> areas(tris.size(), 0.0);
		for (size_t i = 0; i < tris.size(); ++i) {
			const Triangle& t = tris[i];
			const Vec3 e1 = Sub(pos[t.g[1]], pos[t.g[0]]);
			const Vec3 e2 = Sub(pos[t.g[2]], pos[t.g[0]]);
			const Vec3 n = Cross(e1, e2);
			const double len = Length(n);
			if (len <= 0.0) {
				continue;
			}
			areas[i] = 0.5 * len;
			const double a = n.x / len, b = n.y / len, c = n.z / len;
			const double d = -(a * pos[t.g[0]].x + b * pos[t.g[0]].y + c * pos[t.g[0]].z);
			for (uint32_t g : t.g) {
				q[g].AddPlane(a, b, c, d, areas[i]);
			}
		}

		//Open boundaries. An edge used by one triangle bounds the surface, and the
		//quadric of the two vertices on it knows nothing about that - the planes
		//meeting there all belong to the same side, so sliding along the boundary
		//costs nothing and the silhouette erodes first. The fix is the standard one:
		//add the plane through the edge perpendicular to the face, so leaving the
		//boundary line is what costs.
		std::unordered_map<uint64_t, std::pair<int, uint32_t>> edges;
		edges.reserve(tris.size() * 3);
		auto edge_key = [](uint32_t a, uint32_t b) {
			return (a < b) ? (((uint64_t)a << 32) | b) : (((uint64_t)b << 32) | a);
		};
		for (size_t i = 0; i < tris.size(); ++i) {
			for (int e = 0; e < 3; ++e) {
				const uint64_t key = edge_key(tris[i].g[e], tris[i].g[(e + 1) % 3]);
				auto it = edges.find(key);
				if (it == edges.end()) {
					edges.emplace(key, std::make_pair(1, (uint32_t)i));
				}
				else {
					it->second.first++;
				}
			}
		}
		for (const auto& entry : edges) {
			if (entry.second.first != 1) {
				continue;
			}
			const uint32_t a = (uint32_t)(entry.first >> 32);
			const uint32_t b = (uint32_t)(entry.first & 0xffffffffu);
			const Triangle& t = tris[entry.second.second];
			const Vec3 fn = Cross(Sub(pos[t.g[1]], pos[t.g[0]]), Sub(pos[t.g[2]], pos[t.g[0]]));
			const Vec3 edge = Sub(pos[b], pos[a]);
			Vec3 n = Cross(edge, fn);
			const double len = Length(n);
			if (len <= 0.0) {
				continue;
			}
			n.x /= len; n.y /= len; n.z /= len;
			const double d = -(n.x * pos[a].x + n.y * pos[a].y + n.z * pos[a].z);
			const double w = areas[entry.second.second] * BORDER_WEIGHT;
			q[a].AddPlane(n.x, n.y, n.z, d, w);
			q[b].AddPlane(n.x, n.y, n.z, d, w);
		}

		//Texture seams: one position carrying two UVs. Not "one position carrying
		//several corners", which is nearly every vertex of an imported mesh - the
		//corners exist so each face can hold its own normal.
		for (size_t g = 0; g < corners.size(); ++g) {
			const std::vector<uint32_t>& list = corners[g];
			for (size_t i = 1; i < list.size() && !seam[g]; ++i) {
				const Vertex& a = in_v[list[0]];
				const Vertex& b = in_v[list[i]];
				if (fabsf(a.UV.x - b.UV.x) > UV_EPSILON || fabsf(a.UV.y - b.UV.y) > UV_EPSILON ||
					fabsf(a.MeshUV.x - b.MeshUV.x) > UV_EPSILON ||
					fabsf(a.MeshUV.y - b.MeshUV.y) > UV_EPSILON) {
					seam[g] = true;
				}
			}
		}
		return true;
	}

	std::vector<uint32_t> Simplifier::Neighbours(uint32_t v) const {
		std::vector<uint32_t> out;
		for (uint32_t ti : vtris[v]) {
			const Triangle& t = tris[ti];
			if (t.dead) {
				continue;
			}
			for (uint32_t g : t.g) {
				if (g != v) {
					out.push_back(g);
				}
			}
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
		return out;
	}

	double Simplifier::EvalCollapse(uint32_t from, uint32_t to) const {
		//The link condition. Two vertices may only be merged if the only vertices
		//they share are the ones opposite the edge itself; anything else means the
		//two are joined somewhere other than along this edge, and merging them pinches
		//the surface into a shape no longer describable by a triangle list - a hole
		//that cannot be filled and a normal that has no side to point out of.
		int shared = 0;
		for (uint32_t ti : vtris[from]) {
			const Triangle& t = tris[ti];
			if (!t.dead && SharesTriangle(t, to)) {
				shared++;
			}
		}
		if (shared == 0) {
			return COST_FORBIDDEN;
		}
		const std::vector<uint32_t> na = Neighbours(from);
		const std::vector<uint32_t> nb = Neighbours(to);
		std::vector<uint32_t> common;
		std::set_intersection(na.begin(), na.end(), nb.begin(), nb.end(),
			std::back_inserter(common));
		if ((int)common.size() != shared) {
			return COST_FORBIDDEN;
		}

		//Would any triangle that survives be turned inside out? Every one keeping its
		//winding is what makes the coarse mesh still a surface rather than a bag of
		//overlapping faces.
		for (uint32_t ti : vtris[from]) {
			const Triangle& t = tris[ti];
			if (t.dead || SharesTriangle(t, to)) {
				continue;
			}
			Vec3 p[3];
			for (int i = 0; i < 3; ++i) {
				p[i] = (t.g[i] == from) ? pos[to] : pos[t.g[i]];
			}
			const Vec3 after = Cross(Sub(p[1], p[0]), Sub(p[2], p[0]));
			const double after_len = Length(after);
			if (after_len <= 0.0) {
				return COST_FORBIDDEN;
			}
			const Vec3 before = Cross(Sub(pos[t.g[1]], pos[t.g[0]]), Sub(pos[t.g[2]], pos[t.g[0]]));
			const double before_len = Length(before);
			if (before_len <= 0.0) {
				continue;
			}
			if (Dot(before, after) / (before_len * after_len) <= FLIP_COSINE) {
				return COST_FORBIDDEN;
			}
		}

		Quadric sum = q[from];
		sum.Add(q[to]);
		double cost = sum.Eval(pos[to].x, pos[to].y, pos[to].z);
		//Rounding can put a point a hair on the wrong side of every plane through it.
		if (cost < 0.0) {
			cost = 0.0;
		}
		if (seam[from]) {
			cost *= SEAM_PENALTY;
		}
		return cost;
	}

	uint32_t Simplifier::BestCorner(uint32_t to, uint32_t corner) const {
		const std::vector<uint32_t>& list = corners[to];
		uint32_t best = list[0];
		//Parenthesized: <windows.h> defines max as a macro, and an unguarded call
		//here is expanded by it into a syntax error.
		float best_d = (std::numeric_limits<float>::max)();
		const Vertex& want = in_v[corner];
		for (uint32_t candidate : list) {
			const Vertex& v = in_v[candidate];
			//UV first, and by a long way: a corner that lands in the wrong island of
			//the texture map draws a piece of another part of the model, while one
			//whose normal is a few degrees off only shades slightly differently.
			const float du = v.UV.x - want.UV.x, dv = v.UV.y - want.UV.y;
			const float dn = (v.Normal.x - want.Normal.x) * (v.Normal.x - want.Normal.x) +
				(v.Normal.y - want.Normal.y) * (v.Normal.y - want.Normal.y) +
				(v.Normal.z - want.Normal.z) * (v.Normal.z - want.Normal.z);
			const float d = (du * du + dv * dv) * 1000.0f + dn;
			if (d < best_d) {
				best_d = d;
				best = candidate;
			}
		}
		return best;
	}

	void Simplifier::Collapse(uint32_t from, uint32_t to) {
		for (uint32_t ti : vtris[from]) {
			Triangle& t = tris[ti];
			if (t.dead) {
				continue;
			}
			if (SharesTriangle(t, to)) {
				//The two triangles along the edge fold to nothing and go.
				t.dead = true;
				for (uint32_t corner : t.c) {
					ReleaseCorner(corner);
				}
				continue;
			}
			for (int i = 0; i < 3; ++i) {
				if (t.g[i] == from) {
					t.g[i] = to;
					const uint32_t corner = BestCorner(to, t.c[i]);
					AcquireCorner(corner);
					ReleaseCorner(t.c[i]);
					t.c[i] = corner;
				}
			}
			vtris[to].push_back(ti);
		}
		vtris[from].clear();
		dead[from] = true;
		live--;
		q[to].Add(q[from]);
		//The seam (or boundary) that ran through the vertex now runs through the one
		//it became, and has to keep costing what it did.
		if (seam[from]) {
			seam[to] = true;
		}
	}

	void Simplifier::PushEdge(uint32_t a, uint32_t b) {
		if (dead[a] || dead[b] || a == b) {
			return;
		}
		//Both directions are candidates and they are not equally good: collapsing the
		//detailed side into the flat one keeps the surface, the other way round eats
		//it. Only the cheaper of the two is queued.
		const double ca = EvalCollapse(a, b);
		const double cb = EvalCollapse(b, a);
		if (ca <= cb) {
			if (ca != COST_FORBIDDEN) {
				heap.push(HeapItem{ ca, a, b });
			}
		}
		else if (cb != COST_FORBIDDEN) {
			heap.push(HeapItem{ cb, b, a });
		}
	}

	void Simplifier::PushEdgesAround(uint32_t v) {
		for (uint32_t ti : vtris[v]) {
			const Triangle& t = tris[ti];
			if (t.dead) {
				continue;
			}
			for (int i = 0; i < 3; ++i) {
				PushEdge(t.g[i], t.g[(i + 1) % 3]);
			}
		}
	}

	void Simplifier::Run(size_t target) {
		for (const Triangle& t : tris) {
			for (int i = 0; i < 3; ++i) {
				//Every edge is queued twice, once from each triangle sharing it. The
				//duplicate costs one heap entry and is dropped when it surfaces (its
				//`from` is dead by then, or its cost is re-checked and it collapses
				//something legal); de-duplicating it up front would cost a set the
				//size of the mesh for the same result.
				PushEdge(t.g[i], t.g[(i + 1) % 3]);
			}
		}
		//A ceiling on the work, not on the result: every pop either collapses (which
		//can happen `live` times) or drops/re-queues a stale entry, and a re-queue can
		//only happen when a cost went up. The bound is generous enough never to be
		//reached by a mesh that is reducing and tight enough to end one that is not.
		size_t budget = 64 * (tris.size() + pos.size()) + 1024;
		while (live_corners > target && live > 4 && !heap.empty() && budget-- > 0) {
			const HeapItem item = heap.top();
			heap.pop();
			if (dead[item.from] || dead[item.to]) {
				continue;
			}
			//Re-checked rather than trusted. The entry was costed against a
			//neighbourhood that may have been collapsed since, and re-evaluating on
			//the way out is both cheaper and safer than tracking which of the
			//thousands of queued entries a given collapse invalidated.
			const double cost = EvalCollapse(item.from, item.to);
			if (cost == COST_FORBIDDEN) {
				continue;
			}
			if (cost > item.cost * 1.0001) {
				heap.push(HeapItem{ cost, item.from, item.to });
				continue;
			}
			const uint32_t to = item.to;
			Collapse(item.from, to);
			PushEdgesAround(to);
		}
	}

	void Simplifier::Emit(SimplifyResult& out) const {
		std::unordered_map<uint32_t, uint32_t> remap;
		remap.reserve(pos.size() * 2);
		for (const Triangle& t : tris) {
			if (t.dead) {
				continue;
			}
			uint32_t idx[3] = {};
			bool degenerate = false;
			for (int i = 0; i < 3; ++i) {
				auto it = remap.find(t.c[i]);
				if (it == remap.end()) {
					idx[i] = (uint32_t)out.vertices.size();
					remap.emplace(t.c[i], idx[i]);
					out.vertices.push_back(in_v[t.c[i]]);
					out.source_vertex.push_back(t.c[i]);
				}
				else {
					idx[i] = it->second;
				}
			}
			for (int i = 0; i < 3 && !degenerate; ++i) {
				degenerate = (idx[i] == idx[(i + 1) % 3]);
			}
			if (degenerate) {
				continue;
			}
			out.indices.push_back(idx[0]);
			out.indices.push_back(idx[1]);
			out.indices.push_back(idx[2]);
		}
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {

			bool SimplifyMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				float target_ratio,
				SimplifyResult& result) {
				result = SimplifyResult{};
				if (vertices.empty() || indices.size() < 3 || target_ratio <= 0.0f ||
					target_ratio >= 1.0f) {
					return false;
				}
				Simplifier s(vertices, indices);
				if (!s.Build()) {
					LOG_WARN("SimplifyMesh: mesh has no triangles to reduce");
					return false;
				}
				const size_t initial = s.LiveVertexCount();
				//Floored at the twelve corners of a tetrahedron, the smallest thing
				//that still encloses a volume. Below that the reduction has not
				//produced a coarse model, it has produced a plane.
				size_t target = (size_t)((double)initial * (double)target_ratio);
				if (target < 12) {
					target = 12;
				}
				if (target >= initial) {
					return false;
				}
				s.Run(target);
				const size_t remaining = s.LiveVertexCount();
				//How far it actually got. A model can refuse to reduce - a shell of
				//disconnected triangles has no legal collapse anywhere - and handing
				//back a "level of detail" that is the mesh again would put a second
				//copy of the model in the vertex buffer to draw exactly what LOD0
				//draws.
				const double wanted = (double)(initial - target);
				const double got = (double)(initial - remaining);
				if (got < wanted * 0.5) {
					LOG_WARN("SimplifyMesh: reached %zu vertices of %zu asked for (from %zu); "
						"the mesh has no more legal collapses", remaining, target, initial);
					return false;
				}
				s.Emit(result);
				return !result.indices.empty();
			}
		}
	}
}

namespace {
	// What one pass of the clustering makes at a cell size: the cell of every vertex, and how many cells the
	// surviving triangles use. Deterministic: a cell is numbered by the first vertex that falls in it.
	struct ClusterPass {
		std::vector<uint32_t> cell_of;
		std::vector<uint32_t> triangles;
		size_t used = 0;
	};

	struct CellKey {
		int32_t x = 0, y = 0, z = 0;
		bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
	};
	struct CellKeyHash {
		size_t operator()(const CellKey& k) const {
			size_t h = 1469598103934665603ULL;
			for (int32_t w : { k.x, k.y, k.z }) {
				h ^= (size_t)(uint32_t)w;
				h *= 1099511628211ULL;
			}
			return h;
		}
	};

	void RunCluster(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const float3& lo, float cell,
		ClusterPass& pass) {
		pass.cell_of.assign(vertices.size(), 0);
		pass.triangles.clear();
		std::unordered_map<CellKey, uint32_t, CellKeyHash> cells;
		cells.reserve(vertices.size());
		const double inv = 1.0 / (double)cell;
		for (size_t i = 0; i < vertices.size(); ++i) {
			const Vertex& v = vertices[i];
			const CellKey key = { (int32_t)std::floor((v.Position.x - lo.x) * inv), (int32_t)std::floor((v.Position.y - lo.y) * inv),
				(int32_t)std::floor((v.Position.z - lo.z) * inv) };
			auto it = cells.find(key);
			if (it == cells.end()) {
				it = cells.emplace(key, (uint32_t)cells.size()).first;
			}
			pass.cell_of[i] = it->second;
		}
		// The surviving triangles, once each: two that land on the same three cells are one surface.
		struct Tri {
			uint32_t a, b, c;
			bool operator==(const Tri& o) const { return a == o.a && b == o.b && c == o.c; }
		};
		struct TriHash {
			size_t operator()(const Tri& t) const {
				return (size_t)t.a * 73856093u ^ (size_t)t.b * 19349663u ^ (size_t)t.c * 83492791u;
			}
		};
		std::unordered_set<Tri, TriHash> seen;
		std::vector<bool> used(cells.size(), false);
		pass.used = 0;
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const uint32_t a = pass.cell_of[indices[t]], b = pass.cell_of[indices[t + 1]], c = pass.cell_of[indices[t + 2]];
			if (a == b || b == c || a == c) {
				continue;
			}
			// The same three cells in any order, the same winding: a rotation of the triple is the same face.
			Tri key = { a, b, c };
			if (b < key.a && b < c) { key = { b, c, a }; }
			else if (c < key.a && c < b) { key = { c, a, b }; }
			if (!seen.insert(key).second) {
				continue;
			}
			pass.triangles.push_back(a);
			pass.triangles.push_back(b);
			pass.triangles.push_back(c);
			for (uint32_t cell_index : { a, b, c }) {
				if (!used[cell_index]) {
					used[cell_index] = true;
					++pass.used;
				}
			}
		}
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {

			bool ClusterMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				size_t target_vertices,
				SimplifyResult& result) {
				result = SimplifyResult{};
				if (vertices.empty() || indices.size() < 3) {
					return false;
				}
				// Bone weights belong to the vertices of the model, not to an average of them.
				for (const Vertex& v : vertices) {
					if (v.Boneids[0] >= 0) {
						return false;
					}
				}
				if (target_vertices < 12) {
					target_vertices = 12;
				}
				if (target_vertices >= vertices.size()) {
					return false;
				}
				float3 lo = vertices[0].Position, hi = vertices[0].Position;
				for (const Vertex& v : vertices) {
					lo = { (std::min)(lo.x, v.Position.x), (std::min)(lo.y, v.Position.y), (std::min)(lo.z, v.Position.z) };
					hi = { (std::max)(hi.x, v.Position.x), (std::max)(hi.y, v.Position.y), (std::max)(hi.z, v.Position.z) };
				}
				const float extent = (std::max)({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z });
				if (!(extent > 0.0f)) {
					return false;
				}
				// The coarsest cell that still leaves no more than the target: a bisection on the cell size, a pass
				// per step. The count of cells in use falls as the cell grows, not strictly, but near enough that
				// the bisection ends on a size no worse than the best of the sizes it tried.
				float fine = extent / 4096.0f, coarse = extent;
				ClusterPass best;
				bool have = false;
				for (int step = 0; step < 24; ++step) {
					const float cell = 0.5f * (fine + coarse);
					ClusterPass pass;
					RunCluster(vertices, indices, lo, cell, pass);
					if (pass.used > target_vertices || pass.triangles.empty()) {
						if (pass.triangles.empty()) {
							coarse = cell;
						}
						else {
							fine = cell;
						}
					}
					else {
						coarse = cell;
						best = std::move(pass);
						have = true;
					}
				}
				if (!have || best.triangles.empty()) {
					return false;
				}

				// One vertex per cell in use, in the order the triangles first name them.
				std::unordered_map<uint32_t, uint32_t> remap;
				remap.reserve(best.used * 2);
				std::vector<uint32_t> cell_order;
				for (uint32_t cell : best.triangles) {
					if (remap.emplace(cell, (uint32_t)cell_order.size()).second) {
						cell_order.push_back(cell);
					}
				}
				struct Sum {
					double x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
					uint32_t count = 0;
				};
				std::vector<Sum> sums(cell_order.size());
				for (size_t i = 0; i < vertices.size(); ++i) {
					auto it = remap.find(best.cell_of[i]);
					if (it == remap.end()) {
						continue;
					}
					Sum& s = sums[it->second];
					const Vertex& v = vertices[i];
					s.x += v.Position.x; s.y += v.Position.y; s.z += v.Position.z;
					s.nx += v.Normal.x; s.ny += v.Normal.y; s.nz += v.Normal.z;
					++s.count;
				}
				// The member nearest the average lends its UV and frame.
				std::vector<uint32_t> donor(cell_order.size(), UINT32_MAX);
				std::vector<double> donor_d(cell_order.size(), 1e300);
				for (size_t i = 0; i < vertices.size(); ++i) {
					auto it = remap.find(best.cell_of[i]);
					if (it == remap.end()) {
						continue;
					}
					const Sum& s = sums[it->second];
					const double dx = vertices[i].Position.x - s.x / s.count, dy = vertices[i].Position.y - s.y / s.count,
						dz = vertices[i].Position.z - s.z / s.count;
					const double d = dx * dx + dy * dy + dz * dz;
					if (d < donor_d[it->second]) {
						donor_d[it->second] = d;
						donor[it->second] = (uint32_t)i;
					}
				}
				result.vertices.resize(cell_order.size());
				result.source_vertex.resize(cell_order.size());
				for (size_t k = 0; k < cell_order.size(); ++k) {
					Vertex v = vertices[donor[k]];
					const Sum& s = sums[k];
					v.Position = { (float)(s.x / s.count), (float)(s.y / s.count), (float)(s.z / s.count) };
					const double n = std::sqrt(s.nx * s.nx + s.ny * s.ny + s.nz * s.nz);
					if (n > 1e-12) {
						v.Normal = { (float)(s.nx / n), (float)(s.ny / n), (float)(s.nz / n) };
					}
					result.vertices[k] = v;
					result.source_vertex[k] = donor[k];
				}
				result.indices.reserve(best.triangles.size());
				for (uint32_t cell : best.triangles) {
					result.indices.push_back(remap[cell]);
				}
				return true;
			}
		}
	}
}

namespace {
	struct P3 { double x, y, z; };
	P3 Sub3(const P3& a, const P3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	double Dot3(const P3& a, const P3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	P3 Pos3(const Vertex& v) { return { v.Position.x, v.Position.y, v.Position.z }; }

	// The point of triangle abc nearest p (Ericson, Real-Time Collision Detection 5.1.5).
	P3 ClosestPointOnTriangle(const P3& p, const P3& a, const P3& b, const P3& c) {
		const P3 ab = Sub3(b, a), ac = Sub3(c, a), ap = Sub3(p, a);
		const double d1 = Dot3(ab, ap), d2 = Dot3(ac, ap);
		if (d1 <= 0 && d2 <= 0) return a;
		const P3 bp = Sub3(p, b);
		const double d3 = Dot3(ab, bp), d4 = Dot3(ac, bp);
		if (d3 >= 0 && d4 <= d3) return b;
		const double vc = d1 * d4 - d3 * d2;
		if (vc <= 0 && d1 >= 0 && d3 <= 0) {
			const double v = d1 / (d1 - d3);
			return { a.x + v * ab.x, a.y + v * ab.y, a.z + v * ab.z };
		}
		const P3 cp = Sub3(p, c);
		const double d5 = Dot3(ab, cp), d6 = Dot3(ac, cp);
		if (d6 >= 0 && d5 <= d6) return c;
		const double vb = d5 * d2 - d1 * d6;
		if (vb <= 0 && d2 >= 0 && d6 <= 0) {
			const double w = d2 / (d2 - d6);
			return { a.x + w * ac.x, a.y + w * ac.y, a.z + w * ac.z };
		}
		const double va = d3 * d6 - d5 * d4;
		if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
			const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			return { b.x + w * (c.x - b.x), b.y + w * (c.y - b.y), b.z + w * (c.z - b.z) };
		}
		const double denom = 1.0 / (va + vb + vc);
		const double v = vb * denom, w = vc * denom;
		return { a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w };
	}
}
namespace {
	// Triangle against a box (Akenine-Moeller's separating axis test): does the triangle touch the box at all.
	bool TriangleTouchesBox(const double v0[3], const double v1[3], const double v2[3], const double c[3], double h) {
		double a[3][3];
		for (int k = 0; k < 3; ++k) {
			a[0][k] = v0[k] - c[k];
			a[1][k] = v1[k] - c[k];
			a[2][k] = v2[k] - c[k];
		}
		// The box's three face normals.
		for (int k = 0; k < 3; ++k) {
			const double lo = (std::min)({ a[0][k], a[1][k], a[2][k] });
			const double hi = (std::max)({ a[0][k], a[1][k], a[2][k] });
			if (lo > h || hi < -h) {
				return false;
			}
		}
		// The triangle's plane.
		double e0[3], e1[3], e2[3];
		for (int k = 0; k < 3; ++k) {
			e0[k] = a[1][k] - a[0][k];
			e1[k] = a[2][k] - a[1][k];
			e2[k] = a[0][k] - a[2][k];
		}
		const double n[3] = { e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0] };
		const double r = h * (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]));
		const double d = n[0] * a[0][0] + n[1] * a[0][1] + n[2] * a[0][2];
		if (std::fabs(d) > r) {
			return false;
		}
		// The nine cross products of an edge with a box axis.
		const double* edges[3] = { e0, e1, e2 };
		for (const double* e : edges) {
			for (int axis = 0; axis < 3; ++axis) {
				// axis x edge
				double ax[3] = { 0, 0, 0 };
				ax[axis] = 1.0;
				const double q[3] = { ax[1] * e[2] - ax[2] * e[1], ax[2] * e[0] - ax[0] * e[2], ax[0] * e[1] - ax[1] * e[0] };
				const double p0 = q[0] * a[0][0] + q[1] * a[0][1] + q[2] * a[0][2];
				const double p1 = q[0] * a[1][0] + q[1] * a[1][1] + q[2] * a[1][2];
				const double p2 = q[0] * a[2][0] + q[1] * a[2][1] + q[2] * a[2][2];
				const double rad = h * (std::fabs(q[0]) + std::fabs(q[1]) + std::fabs(q[2]));
				if ((std::min)({ p0, p1, p2 }) > rad || (std::max)({ p0, p1, p2 }) < -rad) {
					return false;
				}
			}
		}
		return true;
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {

			bool VoxelizeMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				int resolution,
				int max_triangles,
				SimplifyResult& result) {
				result = SimplifyResult{};
				if (vertices.empty() || indices.size() < 3 || resolution < 2) {
					return false;
				}
				for (const Vertex& v : vertices) {
					if (v.Boneids[0] >= 0) {
						return false;
					}
				}
				double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
				for (const Vertex& v : vertices) {
					const double p[3] = { v.Position.x, v.Position.y, v.Position.z };
					for (int k = 0; k < 3; ++k) {
						lo[k] = (std::min)(lo[k], p[k]);
						hi[k] = (std::max)(hi[k], p[k]);
					}
				}
				const double extent = (std::max)({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] });
				if (!(extent > 0.0)) {
					return false;
				}
				const double cell = extent / (double)resolution;
				// One empty cell of padding all round, so the outside is one piece the flood can start in.
				int dim[3];
				double origin[3];
				for (int k = 0; k < 3; ++k) {
					dim[k] = (int)std::ceil((hi[k] - lo[k]) / cell) + 2;
					origin[k] = lo[k] - cell;
				}
				const size_t count = (size_t)dim[0] * dim[1] * dim[2];
				enum : uint8_t { EMPTY = 0, SURFACE = 1, OUTSIDE = 2 };
				std::vector<uint8_t> grid(count, EMPTY);
				std::vector<std::vector<uint32_t>> cell_triangles(count);
				auto at = [&](int x, int y, int z) -> size_t { return ((size_t)z * dim[1] + y) * dim[0] + x; };

				// Every cell a triangle touches. A hair of slack on the box, so a triangle lying exactly on a cell's
				// face (a flat wall on the grid) marks both cells beside it and cannot slip between them.
				const double half = cell * 0.5 * 1.0001;
				for (size_t t = 0; t + 2 < indices.size(); t += 3) {
					const Vertex* tv[3] = { &vertices[indices[t]], &vertices[indices[t + 1]], &vertices[indices[t + 2]] };
					double p[3][3];
					int from[3], to[3];
					for (int k = 0; k < 3; ++k) {
						p[0][k] = k == 0 ? tv[0]->Position.x : (k == 1 ? tv[0]->Position.y : tv[0]->Position.z);
						p[1][k] = k == 0 ? tv[1]->Position.x : (k == 1 ? tv[1]->Position.y : tv[1]->Position.z);
						p[2][k] = k == 0 ? tv[2]->Position.x : (k == 1 ? tv[2]->Position.y : tv[2]->Position.z);
						const double a = (std::min)({ p[0][k], p[1][k], p[2][k] }), b = (std::max)({ p[0][k], p[1][k], p[2][k] });
						from[k] = (std::max)(0, (int)std::floor((a - origin[k]) / cell));
						to[k] = (std::min)(dim[k] - 1, (int)std::floor((b - origin[k]) / cell));
					}
					for (int z = from[2]; z <= to[2]; ++z) {
						for (int y = from[1]; y <= to[1]; ++y) {
							for (int x = from[0]; x <= to[0]; ++x) {
								const double c[3] = { origin[0] + (x + 0.5) * cell, origin[1] + (y + 0.5) * cell, origin[2] + (z + 0.5) * cell };
								if (TriangleTouchesBox(p[0], p[1], p[2], c, half)) {
									grid[at(x, y, z)] = SURFACE;
									cell_triangles[at(x, y, z)].push_back((uint32_t)(t / 3));
								}
							}
						}
					}
				}

				// The outside: the empty cells that connect to the corner of the grid. Whatever it cannot reach - the
				// inside of a closed model - is solid.
				{
					std::vector<uint32_t> stack;
					stack.push_back((uint32_t)at(0, 0, 0));
					grid[at(0, 0, 0)] = OUTSIDE;
					while (!stack.empty()) {
						const uint32_t i = stack.back();
						stack.pop_back();
						const int x = (int)(i % dim[0]), y = (int)((i / dim[0]) % dim[1]), z = (int)(i / ((size_t)dim[0] * dim[1]));
						const int step[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
						for (const auto& s : step) {
							const int nx = x + s[0], ny = y + s[1], nz = z + s[2];
							if (nx < 0 || ny < 0 || nz < 0 || nx >= dim[0] || ny >= dim[1] || nz >= dim[2]) {
								continue;
							}
							const size_t n = at(nx, ny, nz);
							if (grid[n] == EMPTY) {
								grid[n] = OUTSIDE;
								stack.push_back((uint32_t)n);
							}
						}
					}
				}
				auto solid = [&](int x, int y, int z) {
					if (x < 0 || y < 0 || z < 0 || x >= dim[0] || y >= dim[1] || z >= dim[2]) {
						return false;
					}
					return grid[at(x, y, z)] != OUTSIDE;
				};

				// The surface of the solid, as a surface-nets mesh: the solid cells are the points of a lattice, a vertex
				// sits in every block of 2x2x2 of them that is part solid and part not, and every lattice edge that
				// crosses the boundary gets a quad of the four blocks round it. Unlike a mesh of the cells' faces it has
				// no T-junctions to leave a crack a ray goes through, and its vertices are free to move: each is put on
				// the model, at the mean of the nearest points of the triangles in the cells round it, so the stand-in
				// follows the surface within a fraction of a cell instead of stepping along it.
				const int bdim[3] = { dim[0] - 1, dim[1] - 1, dim[2] - 1 };
				auto bat = [&](int x, int y, int z) -> size_t { return ((size_t)z * bdim[1] + y) * bdim[0] + x; };
				std::vector<int32_t> block_vertex((size_t)bdim[0] * bdim[1] * bdim[2], -1);
				for (int z = 0; z < bdim[2]; ++z) {
					for (int y = 0; y < bdim[1]; ++y) {
						for (int x = 0; x < bdim[0]; ++x) {
							int inside = 0;
							for (int c = 0; c < 8; ++c) {
								inside += solid(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1)) ? 1 : 0;
							}
							if (inside == 0 || inside == 8) {
								continue;
							}
							// The corner the eight cells share, and where the model is round it.
							const P3 centre = { origin[0] + (x + 1) * cell, origin[1] + (y + 1) * cell, origin[2] + (z + 1) * cell };
							P3 sum = { 0, 0, 0 };
							int found = 0;
							const double reach2 = (1.5 * cell) * (1.5 * cell);
							for (int c = 0; c < 8; ++c) {
								const int cx = x + (c & 1), cy = y + ((c >> 1) & 1), cz = z + ((c >> 2) & 1);
								for (uint32_t tri : cell_triangles[at(cx, cy, cz)]) {
									const P3 a3 = Pos3(vertices[indices[tri * 3]]), b3 = Pos3(vertices[indices[tri * 3 + 1]]),
										c3 = Pos3(vertices[indices[tri * 3 + 2]]);
									const P3 q = ClosestPointOnTriangle(centre, a3, b3, c3);
									const P3 d = Sub3(q, centre);
									if (Dot3(d, d) <= reach2) {
										sum = { sum.x + q.x, sum.y + q.y, sum.z + q.z };
										++found;
									}
								}
							}
							P3 pos = centre;
							if (found > 0) {
								pos = { sum.x / found, sum.y / found, sum.z / found };
								// Never out of the block it stands for, or neighbours cross.
								pos = { (std::max)(centre.x - cell, (std::min)(centre.x + cell, pos.x)),
									(std::max)(centre.y - cell, (std::min)(centre.y + cell, pos.y)),
									(std::max)(centre.z - cell, (std::min)(centre.z + cell, pos.z)) };
							}
							block_vertex[bat(x, y, z)] = (int32_t)result.vertices.size();
							Vertex vert;
							vert.Position = { (float)pos.x, (float)pos.y, (float)pos.z };
							result.vertices.push_back(vert);
							result.source_vertex.push_back(0);
						}
					}
				}
				// A quad for every lattice edge with a solid end and an open one: the four blocks that share it.
				for (int axis = 0; axis < 3; ++axis) {
					const int u = (axis + 1) % 3, v = (axis + 2) % 3;
					for (int z = 0; z < dim[2]; ++z) {
						for (int y = 0; y < dim[1]; ++y) {
							for (int x = 0; x < dim[0]; ++x) {
								int p[3] = { x, y, z };
								int q[3] = { x, y, z };
								q[axis] += 1;
								if (q[axis] >= dim[axis] || p[u] < 1 || p[v] < 1 || p[u] >= dim[u] || p[v] >= dim[v]) {
									continue;
								}
								const bool a_in = solid(p[0], p[1], p[2]), b_in = solid(q[0], q[1], q[2]);
								if (a_in == b_in) {
									continue;
								}
								// The blocks round the edge, in the order that goes round it.
								int32_t quad[4];
								const int du[4] = { -1, 0, 0, -1 }, dv[4] = { -1, -1, 0, 0 };
								bool whole = true;
								for (int k = 0; k < 4; ++k) {
									int b[3] = { p[0], p[1], p[2] };
									b[u] += du[k];
									b[v] += dv[k];
									if (b[0] < 0 || b[1] < 0 || b[2] < 0 || b[0] >= bdim[0] || b[1] >= bdim[1] || b[2] >= bdim[2]) {
										whole = false;
										break;
									}
									quad[k] = block_vertex[bat(b[0], b[1], b[2])];
									whole = whole && quad[k] >= 0;
								}
								if (!whole) {
									continue;
								}
								const bool flip = a_in;
								const int order[6] = { 0, 1, 2, 0, 2, 3 };
								for (int k = 0; k < 6; ++k) {
									result.indices.push_back((uint32_t)quad[flip ? order[5 - k] : order[k]]);
								}
							}
						}
					}
				}
				// Normals from the faces, for whatever looks at them.
				{
					std::vector<P3> normals(result.vertices.size(), P3{ 0, 0, 0 });
					for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
						const P3 a3 = Pos3(result.vertices[result.indices[t]]), b3 = Pos3(result.vertices[result.indices[t + 1]]),
							c3 = Pos3(result.vertices[result.indices[t + 2]]);
						const P3 e1 = Sub3(b3, a3), e2 = Sub3(c3, a3);
						const P3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
						for (int k = 0; k < 3; ++k) {
							P3& s = normals[result.indices[t + k]];
							s = { s.x + n.x, s.y + n.y, s.z + n.z };
						}
					}
					for (size_t i = 0; i < normals.size(); ++i) {
						const double len = std::sqrt(Dot3(normals[i], normals[i]));
						if (len > 1e-12) {
							result.vertices[i].Normal = { (float)(normals[i].x / len), (float)(normals[i].y / len), (float)(normals[i].z / len) };
						}
					}
				}				if (result.indices.empty()) {
					result = SimplifyResult{};
					return false;
				}
				if (max_triangles > 0 && result.indices.size() / 3 > (size_t)max_triangles) {
					SimplifyResult reduced;
					const float ratio = (float)max_triangles / (float)(result.indices.size() / 3);
					if (SimplifyMesh(result.vertices, result.indices, ratio, reduced)) {
						reduced.source_vertex.assign(reduced.vertices.size(), 0);
						result = std::move(reduced);
					}
				}
				return true;
			}
		}
	}
}

namespace {

	// The squared distance from p to the triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
	double PointTriangleDistance2(const P3& p, const P3& a, const P3& b, const P3& c) {
		const P3 ab = Sub3(b, a), ac = Sub3(c, a), ap = Sub3(p, a);
		const double d1 = Dot3(ab, ap), d2 = Dot3(ac, ap);
		auto d2of = [&](const P3& q) { const P3 d = Sub3(p, q); return Dot3(d, d); };
		if (d1 <= 0 && d2 <= 0) return d2of(a);
		const P3 bp = Sub3(p, b);
		const double d3 = Dot3(ab, bp), d4 = Dot3(ac, bp);
		if (d3 >= 0 && d4 <= d3) return d2of(b);
		const double vc = d1 * d4 - d3 * d2;
		if (vc <= 0 && d1 >= 0 && d3 <= 0) {
			const double v = d1 / (d1 - d3);
			return d2of({ a.x + v * ab.x, a.y + v * ab.y, a.z + v * ab.z });
		}
		const P3 cp = Sub3(p, c);
		const double d5 = Dot3(ab, cp), d6 = Dot3(ac, cp);
		if (d6 >= 0 && d5 <= d6) return d2of(c);
		const double vb = d5 * d2 - d1 * d6;
		if (vb <= 0 && d2 >= 0 && d6 <= 0) {
			const double w = d2 / (d2 - d6);
			return d2of({ a.x + w * ac.x, a.y + w * ac.y, a.z + w * ac.z });
		}
		const double va = d3 * d6 - d5 * d4;
		if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
			const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			return d2of({ b.x + w * (c.x - b.x), b.y + w * (c.y - b.y), b.z + w * (c.z - b.z) });
		}
		const double denom = 1.0 / (va + vb + vc);
		const double v = vb * denom, w = vc * denom;
		return d2of({ a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w });
	}

	// Points on a mesh to measure from: its vertices and the centres of its triangles, thinned to about `limit`.
	std::vector<P3> SamplePoints(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, size_t limit) {
		std::vector<P3> all;
		all.reserve(vertices.size() + indices.size() / 3);
		// Only the vertices a triangle uses: a model can carry vertices that belong to no surface, and they are
		// not part of what a ray hits.
		std::vector<bool> used(vertices.size(), false);
		for (uint32_t i : indices) {
			if (i < used.size() && !used[i]) {
				used[i] = true;
				all.push_back(Pos3(vertices[i]));
			}
		}
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const P3 a = Pos3(vertices[indices[t]]), b = Pos3(vertices[indices[t + 1]]), c = Pos3(vertices[indices[t + 2]]);
			all.push_back({ (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3 });
		}
		if (all.size() <= limit) {
			return all;
		}
		std::vector<P3> thinned;
		const double step = (double)all.size() / (double)limit;
		for (double i = 0; i < (double)all.size(); i += step) {
			thinned.push_back(all[(size_t)i]);
		}
		return thinned;
	}

	// Distance from each sample to the nearest triangle of a mesh, as mean and worst.
	void DistanceToMesh(const std::vector<P3>& samples, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
		double& sum, double& worst) {
		for (const P3& p : samples) {
			double best = 1e300;
			for (size_t t = 0; t + 2 < indices.size(); t += 3) {
				best = (std::min)(best, PointTriangleDistance2(p, Pos3(vertices[indices[t]]), Pos3(vertices[indices[t + 1]]),
					Pos3(vertices[indices[t + 2]])));
			}
			const double d = std::sqrt(best);
			sum += d;
			worst = (std::max)(worst, d);
		}
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {

			void MeshError(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
				const SimplifyResult& proxy, double& mean, double& worst, double* worst_model_to_proxy, double* worst_proxy_to_model) {
				mean = 0.0;
				worst = 0.0;
				if (vertices.empty() || proxy.vertices.empty()) {
					return;
				}
				double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
				for (const Vertex& v : vertices) {
					const double p[3] = { v.Position.x, v.Position.y, v.Position.z };
					for (int k = 0; k < 3; ++k) {
						lo[k] = (std::min)(lo[k], p[k]);
						hi[k] = (std::max)(hi[k], p[k]);
					}
				}
				const double extent = (std::max)({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] });
				if (!(extent > 0.0)) {
					return;
				}
				double sum_a = 0, worst_a = 0, sum_b = 0, worst_b = 0;
				const std::vector<P3> from_model = SamplePoints(vertices, indices, 4000);
				const std::vector<P3> from_proxy = SamplePoints(proxy.vertices, proxy.indices, 4000);
				DistanceToMesh(from_model, proxy.vertices, proxy.indices, sum_a, worst_a);
				DistanceToMesh(from_proxy, vertices, indices, sum_b, worst_b);
				mean = (std::max)(sum_a / (double)from_model.size(), sum_b / (double)from_proxy.size()) / extent;
				worst = (std::max)(worst_a, worst_b) / extent;
				if (worst_model_to_proxy != nullptr) {
					*worst_model_to_proxy = worst_a / extent;
				}
				if (worst_proxy_to_model != nullptr) {
					*worst_proxy_to_model = worst_b / extent;
				}
			}
		}
	}
}
