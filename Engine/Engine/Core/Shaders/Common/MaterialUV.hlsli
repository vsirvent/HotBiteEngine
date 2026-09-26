#ifndef MATERIAL_UV_HLSLI
#define MATERIAL_UV_HLSLI

//The texture coordinate an ordinary (single) material samples ALL of its maps with.
//
//It lives here, and not inline in the pixel shader, because two stages have to agree on it
//to the last bit: MainRenderPS samples the colour, normal and AO maps with it, and
//MainRenderDS samples the height map with it to displace the surface. When the domain
//shader used the raw mesh UV instead, a material with a UV scale or world-aligned tiling
//showed its colour at one frequency and its relief at another, so the bumps never lined up
//with the bricks painted on them.
//
//  mesh_uv        the interpolated authored UV
//  local_pos      the position in the object's own (pre-`world`) space - only read when
//                 world-aligned tiling is on
//  world_normal   the surface normal in world space (picks which plane to project onto)
//  world          the object's world matrix (only its per-axis scale is used)
//
//World-aligned tiling projects onto whichever plane the surface faces most (dominant axis,
//not blended triplanar - see Material.h's WORLD_UV_ENABLED_FLAG), from the object's local
//position with only its scale re-applied, so a texture keeps its size in world units when
//the object is scaled and turns with it when the object is rotated. Otherwise the mesh UV
//is multiplied by the material's uv_scale (1 is a no-op).
float2 MaterialUV(float2 mesh_uv, float3 local_pos, float3 world_normal, matrix world,
	bool world_aligned, float world_uv_scale, float uv_scale)
{
	//One return, and every path assigns: several early returns out of the world-aligned
	//branch make fxc report the result as potentially uninitialized (X4000).
	float2 uv = mesh_uv * uv_scale;
	if (world_aligned && world_uv_scale > 0.0f) {
		//Each row of `world` is a local axis after scale+rotation, so its length is exactly
		//that axis's scale (Transform never introduces shear).
		float3 world_scale = float3(length(world[0].xyz), length(world[1].xyz), length(world[2].xyz));
		float3 scaled_local_pos = local_pos * world_scale;

		float3 an = abs(world_normal);
		float s = 1.0f / world_uv_scale;
		if (an.x >= an.y && an.x >= an.z)      { uv = scaled_local_pos.zy * s; }
		else if (an.y >= an.x && an.y >= an.z) { uv = scaled_local_pos.xz * s; }
		else                                    { uv = scaled_local_pos.xy * s; }
	}
	return uv;
}

#endif
