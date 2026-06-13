// LPV injection (geometry stage): route each VPL to its grid Z-slice via
// SV_RenderTargetArrayIndex, positioning it at the cell center in the XY slice.
#include "LPVCommon.hlsli"

struct VSOut
{
    float3 flux : FLUX;
    float3 nrm  : NORMALWS;
    int3   cell : CELLPOS;
};

struct GSOut
{
    float4 pos  : SV_Position;
    float3 flux : FLUX;
    float3 nrm  : NORMALWS;
    uint   rt   : SV_RenderTargetArrayIndex;
};

[maxvertexcount(1)]
void main(point VSOut p[1], inout PointStream<GSOut> stream)
{
    if (p[0].cell.x < 0)
        return;
    GSOut o;
    float2 ndc = (float2(p[0].cell.xy) + 0.5f) / float(gLpvSize) * 2.0f - 1.0f;
    o.pos  = float4(ndc.x, -ndc.y, 0.5f, 1.0f);
    o.rt   = uint(p[0].cell.z);
    o.flux = p[0].flux;
    o.nrm  = p[0].nrm;
    stream.Append(o);
}
