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

#include <string>
#include <vector>
#include <Defines.h>
#include "SplatCloud.h"
#include "MeshSimplify.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			// Builds an approximate, low-poly surface mesh out of a splat cloud's points -
			// never for display, only as a stand-in for a shadow-casting proxy (see
			// World::AttachSplatProxy) and a physics collider (World::GenerateSplatProxy).
			//
			// Method: buckets the splats (after dropping ones below a small opacity floor -
			// stray/noise Gaussians should not shape the surface) into a uniform grid sized
			// to `grid_resolution` cells along the cloud's longest axis, then evaluates a
			// signed distance at every grid corner as the opacity/distance-weighted average
			// of the nearby splats' own oriented tangent planes: `dot(corner - splat.position,
			// splat.normal)` (Hoppe et al., "Surface Reconstruction from Unorganized Points").
			// This is why a per-splat normal already exists by the time this runs -
			// SplatCloudData::Load computes one at import for every splat, trained Gaussian or
			// plain point cloud alike - the reconstruction only ever reads it.
			//
			// The isosurface at distance zero is extracted with Surface Nets (Gibson, "Using
			// Distance Maps for Accurate Surface Representation in Sampled Volumes", 1998):
			// one vertex per grid cell whose corners disagree in sign, placed at the average
			// of that cell's sign-crossing edges, connected into quads along grid edges that
			// cross the surface. Chosen over classic Marching Cubes for this specifically
			// because it needs no 256-case lookup table - a hand-transcribed table the size
			// Marching Cubes needs is exactly the kind of thing that goes silently and
			// invisibly wrong - at the cost of a very slightly rounder surface, which does
			// not matter for a mesh nothing ever looks directly at.
			//
			// The raw surface-nets mesh is then handed to the existing Core::SimplifyMesh
			// (MeshSimplify.h) to reach `target_ratio`, exactly as a generated LOD mesh is.
			//
			// `min_dim`/`max_dim` should be the cloud's own bounds (SplatCloudData::
			// min_dimensions/max_dimensions - the 3-sigma ones, not the splat centres alone).
			//
			// False (with `out` left empty) when fewer than 4 splats survive the opacity
			// floor, or the isosurface came out empty (e.g. a grid too coarse for a very
			// thin cloud, or one with no coherent surface at all).
			bool ReconstructSplatMesh(const std::vector<SplatVertex>& splats,
				const float3& min_dim, const float3& max_dim,
				int grid_resolution, float target_ratio,
				SimplifyResult& out, std::string& error);
		}
	}
}
