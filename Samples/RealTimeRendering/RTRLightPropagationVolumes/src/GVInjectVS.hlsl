// Geometry Volume injection (vertex stage): inject RSM surfels as fuzzy occluders
// (SH cosine lobes around the surface normal) at their true surface position.
#include "LPVCommon.hlsli"

Texture2D<float4> gRsmFlux  : register(t1);
Texture2D<float4> gRsmNrm   : register(t2);
Texture2D<float>  gRsmDepth : register(t3);

struct VSOut
{
    float3 nrm  : NORMALWS;
    int3   cell : CELLPOS;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;
    o.cell = int3(-1, -1, -1);
    o.nrm  = float3(0, 1, 0);

    int2 px = int2(vid % uint(gRsmSize), vid / uint(gRsmSize));
    float depth = gRsmDepth.Load(int3(px, 0));
    if (depth >= 1.0f)
        return o;   // empty texel (cleared to far plane)

    float3 n = normalize(gRsmNrm.Load(int3(px, 0)).xyz * 2.0f - 1.0f);

    float2 uv  = (float2(px) + 0.5f) / float(gRsmSize);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 wp  = mul(gInvLightViewProj, float4(ndc, depth, 1.0f));
    float3 pos = wp.xyz / wp.w;   // occluder stays at the true surface position

    int3 cell = int3(floor((pos - gLpvMin) / gCellSize));
    if (any(cell < 0) || any(cell >= gLpvSize))
        return o;

    o.cell = cell;
    o.nrm  = n;
    return o;
}
