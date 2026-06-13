// LPV composite: reconstruct world position from the scene depth, sample the
// propagated LPV, and output the indirect irradiance (x albedo).  The host selects
// an additive PSO for "Final" (added on top of the direct-lit scene) and an opaque
// PSO for the debug views.
#include "LPVCommon.hlsli"

Texture2D<float>  gSceneDepth  : register(t0);
Texture2D<float4> gSceneNormal : register(t1);
Texture2D<float4> gAlbedo      : register(t2);
Texture3D<float4> gLpvR        : register(t3);
Texture3D<float4> gLpvG        : register(t4);
Texture3D<float4> gLpvB        : register(t5);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

float3 IndirectLPV(float3 wpos, float3 n)
{
    // sample half a cell away from the surface to reduce self illumination
    float3 sp  = wpos + n * (0.5f * gCellSize);
    float3 uvw = (sp - gLpvMin) / (gCellSize * float(gLpvSize));
    if (any(uvw < 0.0f) || any(uvw > 1.0f))
        return 0.0f;   // outside the grid

    float4 shN = SHBasis(-n);
    float3 e;
    e.r = dot(shN, gLpvR.SampleLevel(gLinearClamp, uvw, 0));
    e.g = dot(shN, gLpvG.SampleLevel(gLinearClamp, uvw, 0));
    e.b = dot(shN, gLpvB.SampleLevel(gLinearClamp, uvw, 0));
    return max(e, 0.0f);
}

float4 main(VSOut i) : SV_Target0
{
    int2 px = int2(i.pos.xy);

    // The scene normal buffer is cleared to 0, so a zero-length normal marks a
    // background (no-geometry) pixel - skip it for every view mode.
    float3 nraw = gSceneNormal.Load(int3(px, 0)).xyz;
    if (dot(nraw, nraw) < 1e-6f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float3 n      = normalize(nraw);
    float3 albedo = gAlbedo.Load(int3(px, 0)).rgb;

    // reconstruct world position from depth using the inverse camera view-projection
    float  depth = gSceneDepth.Load(int3(px, 0));
    float2 ndc   = float2(i.uv.x * 2.0f - 1.0f, 1.0f - i.uv.y * 2.0f);
    float4 wp    = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    float3 wpos  = wp.xyz / wp.w;

    float3 indirect = IndirectLPV(wpos, n) * gIndirectScale;

    if (gDisplayMode == 3) return float4(albedo, 1.0f);          // Albedo
    if (gDisplayMode == 4) return float4(n * 0.5f + 0.5f, 1.0f); // Normal

    // Final (additive PSO) and Indirect-only (opaque PSO) both emit indirect * albedo.
    return float4(indirect * albedo, 1.0f);
}
