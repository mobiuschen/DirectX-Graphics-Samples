// LPV injection (pixel stage): project the VPL as a clamped cosine lobe in SH and
// emit per-channel coefficients.  Additive blending accumulates overlapping VPLs.
#include "LPVCommon.hlsli"

struct GSOut
{
    float4 pos  : SV_Position;
    float3 flux : FLUX;
    float3 nrm  : NORMALWS;
    uint   rt   : SV_RenderTargetArrayIndex;
};

struct PSOut
{
    float4 shR : SV_Target0;
    float4 shG : SV_Target1;
    float4 shB : SV_Target2;
};

PSOut main(GSOut i)
{
    float4 lobe = SHCosLobe(i.nrm) / PI * gInjectScale;
    PSOut o;
    o.shR = lobe * i.flux.r;
    o.shG = lobe * i.flux.g;
    o.shB = lobe * i.flux.b;
    return o;
}
