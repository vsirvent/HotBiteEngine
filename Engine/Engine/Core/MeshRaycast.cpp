#include <Core/MeshRaycast.h>

#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace HotBite {
	namespace Engine {
		namespace Core {
			namespace {

				//Standard Moller-Trumbore test, entirely in the mesh's own object space.
				//`u`/`v` are the barycentric weights of v1/v2 (v0's is 1-u-v), matching the
				//convention P = (1-u-v)*v0 + u*v1 + v*v2.
				bool RayTriangle(const float3& orig, const float3& dir,
					const float3& v0, const float3& v1, const float3& v2,
					float& out_t, float& out_u, float& out_v) {
					const XMVECTOR O = XMLoadFloat3(&orig);
					const XMVECTOR D = XMLoadFloat3(&dir);
					const XMVECTOR V0 = XMLoadFloat3(&v0);
					const XMVECTOR V1 = XMLoadFloat3(&v1);
					const XMVECTOR V2 = XMLoadFloat3(&v2);
					const XMVECTOR E1 = XMVectorSubtract(V1, V0);
					const XMVECTOR E2 = XMVectorSubtract(V2, V0);
					const XMVECTOR P = XMVector3Cross(D, E2);
					const float det = XMVectorGetX(XMVector3Dot(E1, P));
					if (std::fabs(det) < 1e-9f) {
						return false; //ray parallel to the triangle's plane
					}
					const float inv_det = 1.0f / det;
					const XMVECTOR T = XMVectorSubtract(O, V0);
					const float u = XMVectorGetX(XMVector3Dot(T, P)) * inv_det;
					if (u < 0.0f || u > 1.0f) {
						return false;
					}
					const XMVECTOR Q = XMVector3Cross(T, E1);
					const float v = XMVectorGetX(XMVector3Dot(D, Q)) * inv_det;
					if (v < 0.0f || u + v > 1.0f) {
						return false;
					}
					const float t = XMVectorGetX(XMVector3Dot(E2, Q)) * inv_det;
					if (t < 1e-5f) {
						return false; //behind the ray origin
					}
					out_t = t;
					out_u = u;
					out_v = v;
					return true;
				}
			}

			MeshRayHit RaycastMeshUV(const MeshData& mesh, FXMMATRIX world,
				CXMMATRIX world_inverse, const float3& ray_origin_world,
				const float3& ray_dir_world, float max_distance) {
				MeshRayHit result;
				const size_t triangle_count = mesh.indices.size() / 3;
				if (mesh.vertices.empty() || triangle_count == 0) {
					return result;
				}

				const XMVECTOR origin_world_v = XMLoadFloat3(&ray_origin_world);
				const XMVECTOR origin_local_v = XMVector3TransformCoord(origin_world_v, world_inverse);
				const XMVECTOR dir_local_v = XMVector3Normalize(
					XMVector3TransformNormal(XMVector3Normalize(XMLoadFloat3(&ray_dir_world)), world_inverse));
				float3 origin_local, dir_local;
				XMStoreFloat3(&origin_local, origin_local_v);
				XMStoreFloat3(&dir_local, dir_local_v);

				float best_t = FLT_MAX;
				uint32_t best_i0 = 0, best_i1 = 0, best_i2 = 0;
				float best_u = 0.0f, best_v = 0.0f;
				for (size_t tri = 0; tri < triangle_count; ++tri) {
					const uint32_t i0 = mesh.indices[tri * 3 + 0];
					const uint32_t i1 = mesh.indices[tri * 3 + 1];
					const uint32_t i2 = mesh.indices[tri * 3 + 2];
					float t, u, v;
					if (RayTriangle(origin_local, dir_local, mesh.vertices[i0].Position,
						mesh.vertices[i1].Position, mesh.vertices[i2].Position, t, u, v) &&
						t < best_t) {
						best_t = t;
						best_i0 = i0; best_i1 = i1; best_i2 = i2;
						best_u = u; best_v = v;
					}
				}
				if (best_t == FLT_MAX) {
					return result;
				}

				const float w = 1.0f - best_u - best_v;
				const Vertex& a = mesh.vertices[best_i0];
				const Vertex& b = mesh.vertices[best_i1];
				const Vertex& c = mesh.vertices[best_i2];
				const float3 pos_local = {
					w * a.Position.x + best_u * b.Position.x + best_v * c.Position.x,
					w * a.Position.y + best_u * b.Position.y + best_v * c.Position.y,
					w * a.Position.z + best_u * b.Position.z + best_v * c.Position.z,
				};
				const XMVECTOR pos_world_v = XMVector3TransformCoord(XMLoadFloat3(&pos_local), world);
				//The local-space `t` is not a world-space distance under non-uniform
				//scale (TransformNormal does not preserve length), so the world distance
				//is measured directly from the transformed hit point instead of derived
				//from `best_t`.
				const float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(pos_world_v, origin_world_v)));
				if (distance > max_distance) {
					return result;
				}

				result.hit = true;
				result.distance = distance;
				XMStoreFloat3(&result.position, pos_world_v);
				result.uv = {
					w * a.UV.x + best_u * b.UV.x + best_v * c.UV.x,
					w * a.UV.y + best_u * b.UV.y + best_v * c.UV.y,
				};
				return result;
			}
		}
	}
}
