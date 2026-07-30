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
