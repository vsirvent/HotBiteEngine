#ifndef __INSTANCING_HLSLI__
#define __INSTANCING_HLSLI__

//What one instance of an instanced draw (RenderSystem's DrawBucket) is: where the object stands
//now, and where it stood in the previous rendered frame (the motion vectors need both). A
//vertex shader that can be instanced declares `instanced` and `instance_base` in its constant
//buffer and, when `instanced` is not 0, takes the object's matrices from here at
//instance_base + SV_InstanceID instead of from its own `world`. The layout is
//RenderSystem::InstanceData: two float4x4, in the order the engine keeps Transform's own
//matrices (transposed for HLSL).
struct InstanceData
{
	matrix world;
	matrix prev_world;
};

StructuredBuffer<InstanceData> instances : register(t0);

#endif
