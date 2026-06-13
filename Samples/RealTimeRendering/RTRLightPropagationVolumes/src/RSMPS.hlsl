// Reflective Shadow Map pixel stage: output reflected flux + encoded world normal.
#include "LPVCommon.hlsli"

cbuffer DrawCB : register(b1) { uint gMatFlags; }   // bit0 = alpha masked

Texture2D gAlbedoTex : register(t0);

struct VSOut
{
    float4 pos : SV_Position;
    float3 nrm : NORMALWS;
    float2 uv  : TEXCOORD;
};

struct PSOut
{
    float4 flux   : SV_Target0;   // albedo * sun color * intensity
    float4 normal : SV_Target1;   // world normal * 0.5 + 0.5
};

PSOut main(VSOut i)
{
    float4 alb = gAlbedoTex.Sample(gAniso, i.uv);
    if (gMatFlags & 1u)
        clip(alb.a - 0.5f);

    float3 n = normalize(i.nrm);
    // geometry is rendered two-sided; make the surfel normal face the light
    if (dot(n, -gSunDir) < 0.0f)
        n = -n;

    PSOut o;
    o.flux   = float4(alb.rgb * gSunColor * gSunIntensity, 1.0f);
    o.normal = float4(n * 0.5f + 0.5f, 1.0f);
    return o;
}
