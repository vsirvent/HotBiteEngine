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

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <Core/Vertex.h>

namespace HotBite {
	namespace Engine {
		namespace Core {

			// == Automatic level of detail =========================================
			//
			// Builds a coarser stand-in for a mesh by collapsing its shortest, least
			// significant edges until the vertex count is down to `target_ratio` of
			// what it was - the geometry a MeshData::MeshLod holds, produced from the
			// model itself instead of being authored as a second .fbx.
			//
			// Three decisions shape everything below, and all three are about the
			// result still being usable as the *same* asset rather than about squeezing
			// the last percent of error out of the reduction:
			//
			//  - A surviving vertex is always one of the two the edge had (subset
			//    placement), never the optimal point the quadric solves for. That
			//    optimum is a better fit for the surface and is what an offline tool
			//    does, but it is a position no vertex ever had, so every attribute
			//    riding on it - UV, normal, tangent, and above all the four bone
			//    indices and weights of a skinned model - would have to be invented
			//    for it. Keeping an original vertex keeps all of them exactly right,
			//    which is what lets a generated level be dropped into a chain whose
			//    LOD0 is animating (see CompatibleSkinning in Mesh.cpp).
			//  - Vertices are welded by *position* for the collapse and only for it.
			//    The importer clones a control point per polygon corner (see
			//    MeshData::flat_frames), so the index buffer alone describes a mesh
			//    that is topologically in pieces: an edge collapse on it would move one
			//    corner of a seam and tear the surface open. The corners are carried
			//    along and re-picked per triangle afterwards.
			//  - Open boundaries and UV seams are held back rather than forbidden.
			//    Locking them keeps every reduction honest but stops a heavily seamed
			//    model - which is most game models - from reaching its target at all;
			//    weighting them so they are the last thing to go gets the target and
			//    keeps the silhouette.
			//
			// The output is deterministic for a given input: same mesh, same ratio,
			// same vertices in the same order. That is what makes it cacheable to disk
			// (World::GenerateMeshLod) rather than something a level has to carry.
			struct SimplifyResult {
				std::vector<Vertex> vertices;
				std::vector<uint32_t> indices;
				// Per output vertex, the index of the input vertex it came from. The
				// caller needs it to carry anything held *beside* the vertex across:
				// MeshData's flat frames and smoothing groups, which are what let the
				// generated mesh be re-smoothed with the mesh it stands in for.
				std::vector<uint32_t> source_vertex;
			};

			// `target_ratio` is the share of the input's vertices to keep, in (0, 1).
			// The result lands near it rather than on it - a collapse removes a whole
			// vertex and everything welded to it at once - and is reported by what
			// comes back rather than assumed, which is also how MeshData::MeshLod
			// derives the ratio it selects on.
			//
			// False (with `result` left empty) when there is nothing to do: a mesh with
			// no triangles, a ratio at or above 1, or a reduction that could not reach
			// even half way to the target because every remaining collapse would fold
			// the surface over itself.
			bool SimplifyMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				float target_ratio,
				SimplifyResult& result);

			// A coarser stand-in by vertex clustering, for what SimplifyMesh cannot reach. The edge collapse
			// stops where no collapse is legal - a model of shells and cuts (a Meshy building) stalls at about a
			// tenth of its vertices - and a level that is wanted far coarser than that (the geometry the ray
			// traced shadows are walked through) has to come from somewhere else: the vertices are snapped to a
			// grid, the cell as coarse as it takes to leave `target_vertices` of them, and the triangles that
			// land on fewer than three cells go.
			//
			// Not the optimal reduction by any measure - a vertex is the average of its cell, so unlike
			// SimplifyMesh no output vertex is one of the input's, and a skinned mesh cannot take it - but it
			// always gets there, whatever the topology. The attributes of a vertex (UV, tangent frame) are those
			// of the input vertex nearest the average; `source_vertex` names it.
			//
			// False when there is nothing to do (no triangles, already at or under the target) or the mesh is
			// skinned. `target_vertices` is floored at 12.
			bool ClusterMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				size_t target_vertices,
				SimplifyResult& result);

			// A solid stand-in for a mesh, for what only has to be in the way: the geometry the ray traced
			// shadows are traced through. Neither SimplifyMesh nor ClusterMesh makes one - a model made of
			// shells and cuts (a Meshy building, a tree of leaf cards) comes out of both full of gaps a ray
			// goes through - so this does not reduce the surface, it replaces it: the model is voxelized on a
			// grid of `resolution` cells along its longest side (every cell a triangle touches is solid, so a
			// gap narrower than a cell closes), the space outside it is found by flooding in from the
			// edge of the grid, and the surface of what is not outside is built, flat faces merged into
			// rectangles. What comes out is closed, whatever came in; the price is a blocky look and a
			// surface up to one cell off the model, which a shadow does not mind and a picture would.
			//
			// The surface is a surface-nets mesh of the solid, its vertices moved onto the model (to the mean of the
			// nearest points of the triangles round them), so it follows the model within a fraction of a cell. With
			// `max_triangles` above 0 it is then reduced to about that many by SimplifyMesh, which has no seams to
			// stall on (there are no UVs) and a surface that is closed and manifold.
			//
			// The vertices carry a position and the face's normal and nothing else (no UV, no bones): it is
			// not drawn. False when there is nothing to voxelize or the mesh is skinned.
			bool VoxelizeMesh(const std::vector<Vertex>& vertices,
				const std::vector<uint32_t>& indices,
				int resolution,
				int max_triangles,
				SimplifyResult& result);

			// How far a stand-in is from the model it stands in for: the distance from points on each to the
			// nearest point of the other (sampled: the vertices and the centres of the triangles, at most about
			// 4,000 of each), as a share of the model's longest side. `mean` and `worst` take the larger of the two
			// directions - the proxy's surface lying off the model, and a piece of the model the proxy does not
			// cover. 0 for a perfect copy.
			void MeshError(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
				const SimplifyResult& proxy, double& mean, double& worst, double* worst_model_to_proxy = nullptr,
				double* worst_proxy_to_model = nullptr);
		}
	}
}
