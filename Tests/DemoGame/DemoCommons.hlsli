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

#define MAX_EFFECTS 32
#define MAX_CREATURES 128
#define NGRASS 4
#define MAX_TARGETS 5
//0.8 of UI area + 0.02 of wave effect of water that
//is required to render
#define UI_START_Y 0.82f

struct EffectData {
#define EFFECT_NONE 0
#define EFFECT_FIRE 1
#define EFFECT_EXPLOSION 2
#define EFFECT_FROZEN 3
#define EFFECT_LIGHTING 4	
	unsigned int type;
	float3 origin;
	float3 destination;
	float param0;
	float param1;
	float param2;
	float param3;
};

struct GameObjectGPUData {
#define IS_TERRAIN_FLAG (1 << 0)
#define IS_CREATURE_FLAG (1 << 1)
#define IS_HEALTHBAR_FLAG (1 << 2)
#define APPLY_FOG_WAR_FLAG (1 << 3)
	unsigned int flags;
};

struct SelectableGPUData {
#define SELECTED_FLAG 3
	float4 color;
	unsigned int flags;
};

struct TerrainGPUData {
#define TERRAIN_DEBUG_FLAG (1 << 0)
#define TARGET_FLAG  (1 << 1)
#define TARGET_ATTACK_FLAG  (1 << 2)
#define TARGET_ON_GUARD_FLAG  (1 << 3)
	float4 target_position;
	unsigned int flags;
};

struct CreatureGPUData {
#define IS_FLYING_FLAG (1 << 0)
#define IS_MOVING_FLAG  (1 << 1)
	//percentage
	float health;
	unsigned int flags;
};

struct HealthBarGPUData {
	float4 color;
	float life_ratio;
};

struct TerrainDomainOutput
{
	float4 position		: SV_POSITION;
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float  tess_factor  : TESSFACTOR;
};