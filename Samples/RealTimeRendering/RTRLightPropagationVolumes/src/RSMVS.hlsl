// Reflective Shadow Map: render the scene from the sun (orthographic) capturing
// world normal + reflected flux + depth.  Vertex stage.
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
    float3 nrm : NORMALWS;
    float2 uv  : TEXCOORD;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = mul(gLightViewProj, float4(i.pos, 1.0f));
    o.nrm = i.nrm;
    o.uv  = i.uv;
    return o;
}
