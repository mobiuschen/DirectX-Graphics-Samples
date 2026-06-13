// Geometry Volume injection (pixel stage): emit the blocking potential as a cosine
// lobe around the surfel normal.  Blended with MAX to avoid double counting.
#include "LPVCommon.hlsli"

struct GSOut
{
    float4 pos : SV_Position;
    float3 nrm : NORMALWS;
    uint   rt  : SV_RenderTargetArrayIndex;
};

float4 main(GSOut i) : SV_Target0
{
    return SHCosLobe(i.nrm);
}
