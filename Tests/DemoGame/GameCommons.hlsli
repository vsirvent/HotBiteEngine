
#define NGRASS 4

struct TerrainDomainOutput
{
	float4 position		: SV_POSITION;
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float  tess_factor  : TESSFACTOR;
};