// LPV injection (vertex stage): each RSM texel becomes a Virtual Point Light.
// One point is drawn per RSM texel (SV_VertexID); empty texels collapse to cell -1.
#include "LPVCommon.hlsli"

Texture2D<float4> gRsmFlux  : register(t1);
Texture2D<float4> gRsmNrm   : register(t2);
Texture2D<float>  gRsmDepth : register(t3);

struct VSOut
{
    float3 flux : FLUX;
    float3 nrm  : NORMALWS;
    int3   cell : CELLPOS;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;
    o.cell = int3(-1, -1, -1);
    o.flux = 0;
    o.nrm  = float3(0, 1, 0);

    int2 px = int2(vid % uint(gRsmSize), vid / uint(gRsmSize));

    float3 flux = gRsmFlux.Load(int3(px, 0)).rgb;
    if (dot(flux, flux) < 1e-8f)
        return o;   // no geometry / no reflected light at this texel

    float  depth = gRsmDepth.Load(int3(px, 0));
    float3 n     = normalize(gRsmNrm.Load(int3(px, 0)).xyz * 2.0f - 1.0f);

    // reconstruct world position from RSM depth
    float2 uv  = (float2(px) + 0.5f) / float(gRsmSize);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 wp  = mul(gInvLightViewProj, float4(ndc, depth, 1.0f));
    float3 pos = wp.xyz / wp.w;

    // push the VPL half a cell along its normal to avoid self-illumination
    pos += n * (0.5f * gCellSize);

    int3 cell = int3(floor((pos - gLpvMin) / gCellSize));
    if (any(cell < 0) || any(cell >= gLpvSize))
        return o;

    o.cell = cell;
    o.flux = flux;
    o.nrm  = n;
    return o;
}
