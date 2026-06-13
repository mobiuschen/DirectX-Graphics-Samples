// Albedo G-buffer: depth-equal pass over the scene from the camera, writing albedo
// (used by the composite for the indirect * albedo term).  Vertex stage.
#include "LPVCommon.hlsli"

struct VSIn
{
    float3 pos : POSITION;
    float2 uv  : TEXCOORD;
    float3 nrm : NORMAL;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = mul(gViewProj, float4(i.pos, 1.0f));
    o.uv  = i.uv;
    return o;
}
