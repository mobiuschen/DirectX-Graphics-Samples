//
// LPVCommon.hlsli
//
// Shared constant buffer, spherical-harmonics math, and samplers for the LPV passes.
// The cbuffer layout mirrors LPVConstants (C++).  SH convention (Kaplanyan &
// Dachsbacher 2010): band 0+1, 4 coefficients, layout (Y00, Y1-1, Y10, Y11) with
// directions (1, y, z, x).
//

#ifndef LPV_COMMON_HLSLI
#define LPV_COMMON_HLSLI

static const float PI = 3.14159265359f;

// Matrices use the MiniEngine convention: mul(matrix, vector).  Because DirectXMath
// stores row-major and HLSL packs column-major, a DirectXMath product P stored with
// XMStoreFloat4x4 satisfies mul(P, v) == XMVector4Transform(v, P) in HLSL.
cbuffer LPVFrame : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gLightViewProj;
    float4x4 gInvLightViewProj;

    float3 gCamPos;     float gSunIntensity;
    float3 gSunDir;     float gShadowBias;      // gSunDir = direction light travels (toward scene)
    float3 gSunColor;   float gIndirectScale;
    float3 gLpvMin;     float gCellSize;

    int   gLpvSize;     int   gRsmSize;     int gDisplayMode;   float gInjectScale;
    float gOcclusionAmp; int  gUseGV;       float gExposure;    float gPad0;
}

// SH basis evaluated in direction d.
float4 SHBasis(float3 d)
{
    return float4(0.282094792f, -0.488602512f * d.y, 0.488602512f * d.z, -0.488602512f * d.x);
}

// SH projection of a clamped cosine lobe oriented along direction d.
float4 SHCosLobe(float3 d)
{
    return float4(0.886226925f, -1.023326707f * d.y, 1.023326707f * d.z, -1.023326707f * d.x);
}

SamplerState           gAniso       : register(s0);
SamplerState           gLinearClamp : register(s1);
SamplerComparisonState gShadowCmp   : register(s2);
SamplerState           gPointClamp  : register(s3);

#endif // LPV_COMMON_HLSLI
