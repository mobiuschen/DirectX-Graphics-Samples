// Albedo G-buffer pixel stage.
#include "LPVCommon.hlsli"

cbuffer DrawCB : register(b1) { uint gMatFlags; }   // bit0 = alpha masked

Texture2D gAlbedoTex : register(t0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

float4 main(VSOut i) : SV_Target0
{
    float4 alb = gAlbedoTex.Sample(gAniso, i.uv);
    if (gMatFlags & 1u)
        clip(alb.a - 0.5f);
    return float4(alb.rgb, 1.0f);
}
