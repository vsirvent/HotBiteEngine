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
		}
	}
}
