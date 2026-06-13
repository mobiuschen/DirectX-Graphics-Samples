// LPV propagation (Kaplanyan & Dachsbacher 2010).  Each iteration gathers SH flux
// from the 6 face-adjacent neighbours, re-projecting it through the 5 visible faces
// of the destination cell, modulated by geometry-volume occlusion, and accumulates
// the result into a running total.
#include "LPVCommon.hlsli"

cbuffer PropCB : register(b1) { int gFirstIteration; }

Texture3D<float4> gPrevR    : register(t0);
Texture3D<float4> gPrevG    : register(t1);
Texture3D<float4> gPrevB    : register(t2);
Texture3D<float4> gAccPrevR : register(t3);
Texture3D<float4> gAccPrevG : register(t4);
Texture3D<float4> gAccPrevB : register(t5);
Texture3D<float4> gGV       : register(t6);

RWTexture3D<float4> gNextR    : register(u0);
RWTexture3D<float4> gNextG    : register(u1);
RWTexture3D<float4> gNextB    : register(u2);
RWTexture3D<float4> gAccNextR : register(u3);
RWTexture3D<float4> gAccNextG : register(u4);
RWTexture3D<float4> gAccNextB : register(u5);

// solid angles subtended by the faces of the destination cell as seen from the
// neighbour cell center (steradians), normalized by 4*pi
static const float kDirectW = 0.4006696846f / (4.0f * PI);
static const float kSideW   = 0.4234413544f / (4.0f * PI);

static const float3 kMainDirs[6] =
{
    float3( 1, 0, 0), float3(-1, 0, 0),
    float3( 0, 1, 0), float3( 0,-1, 0),
    float3( 0, 0, 1), float3( 0, 0,-1)
};

static const float3 kSideDirs[6][4] =
{
    { float3(0, 1, 0), float3(0,-1, 0), float3(0, 0, 1), float3(0, 0,-1) },
    { float3(0, 1, 0), float3(0,-1, 0), float3(0, 0, 1), float3(0, 0,-1) },
    { float3(1, 0, 0), float3(-1,0, 0), float3(0, 0, 1), float3(0, 0,-1) },
    { float3(1, 0, 0), float3(-1,0, 0), float3(0, 0, 1), float3(0, 0,-1) },
    { float3(1, 0, 0), float3(-1,0, 0), float3(0, 1, 0), float3(0,-1, 0) },
    { float3(1, 0, 0), float3(-1,0, 0), float3(0, 1, 0), float3(0,-1, 0) }
};

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= uint(gLpvSize)))
        return;

    float4 r = 0, g = 0, b = 0;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float3 o = kMainDirs[i];               // direction of travel: src -> this cell
        int3 src = int3(id) - int3(o);
        if (any(src < 0) || any(src >= gLpvSize))
            continue;

        float4 sr = gPrevR[src];
        float4 sg = gPrevG[src];
        float4 sb = gPrevB[src];

        // fuzzy occlusion from the geometry volume, sampled halfway between cells
        float occ = 1.0f;
        if (gUseGV != 0 && gFirstIteration == 0)
        {
            float3 gvUVW = (float3(id) + 0.5f - 0.5f * o) / float(gLpvSize);
            float4 gv = gGV.SampleLevel(gLinearClamp, gvUVW, 0);
            occ = 1.0f - saturate(gOcclusionAmp * max(0.0f, dot(SHBasis(-o), gv)));
        }

        // ---- direct (far) face ----------------------------------------------
        {
            float4 shO  = SHBasis(o);
            float4 lobe = SHCosLobe(o);
            r += occ * kDirectW * max(0.0f, dot(sr, shO)) * lobe;
            g += occ * kDirectW * max(0.0f, dot(sg, shO)) * lobe;
            b += occ * kDirectW * max(0.0f, dot(sb, shO)) * lobe;
        }

        // ---- 4 side faces ---------------------------------------------------
        [unroll]
        for (int s = 0; s < 4; ++s)
        {
            float3 sd      = kSideDirs[i][s];
            float3 evalDir = normalize(o + 0.5f * sd);
            float4 shE     = SHBasis(evalDir);
            float4 lobe    = SHCosLobe(sd);
            r += occ * kSideW * max(0.0f, dot(sr, shE)) * lobe;
            g += occ * kSideW * max(0.0f, dot(sg, shE)) * lobe;
            b += occ * kSideW * max(0.0f, dot(sb, shE)) * lobe;
        }
    }

    gNextR[id] = r;
    gNextG[id] = g;
    gNextB[id] = b;
    gAccNextR[id] = gAccPrevR[id] + r;
    gAccNextG[id] = gAccPrevG[id] + g;
    gAccNextB[id] = gAccPrevB[id] + b;
}
