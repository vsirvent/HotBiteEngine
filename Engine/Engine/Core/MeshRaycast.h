#pragma once

#include <Defines.h>
#include <Core/Mesh.h>
#include <DirectXMath.h>

namespace HotBite {
	namespace Engine {
		namespace Core {

			struct MeshRayHit {
				bool hit = false;
				float distance = 0.0f; //world units from the ray origin
				float3 position = {};  //world space
				float2 uv = {};        //Vertex::UV (not MeshUV), barycentric-interpolated
			};

			// Brute-force ray-vs-triangle test against `mesh`'s own object-space vertex/
			// index buffers, for editor tools (the mask-paint brush) that need an exact
			// surface hit and its UV rather than the GPU ray tracers' coarsest-LOD, no-UV
			// approximation (see RenderSystem::PrepareRT). No BVH traversal - Core::BVH
			// has no CPU-side ray walk today (it only ever gets uploaded for the GPU
			// tracers to walk), and writing/proving a correct one is a much bigger job
			// than an on-mouse-move editor query needs; this is fine for a terrain/prop
			// scale mesh and is expected to be slow on a very high poly count one.
			//
			// `world`/`world_inverse` must be the entity's Transform::world_xmmatrix and
			// its inverse - NOT world_matrix/world_inv_matrix, which are pre-transposed
			// for HLSL's row-major convention (see CLAUDE.md's splat-cloud note on this
			// exact trap: loading the transposed field here would silently misplace every
			// hit rather than fail to compile). `ray_dir_world` need not be normalized.
			// No hit (`MeshRayHit::hit == false`) when the mesh has no geometry, nothing
			// is in front of the ray, or the nearest hit is farther than `max_distance`.
			MeshRayHit RaycastMeshUV(const MeshData& mesh, DirectX::FXMMATRIX world,
				DirectX::CXMMATRIX world_inverse, const float3& ray_origin_world,
				const float3& ray_dir_world, float max_distance);
		}
	}
}
