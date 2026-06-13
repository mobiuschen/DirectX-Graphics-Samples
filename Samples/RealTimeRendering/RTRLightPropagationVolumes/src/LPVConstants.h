//
// LPVConstants.h
//
// CPU-side mirror of the per-frame constant buffer used by every LPV shader.
// The field order and packing MUST match the `LPVFrame` cbuffer in LPVCommon.hlsli
// (HLSL packs a float3 + float into a single 16-byte register, which is why the
// scalars are interleaved with the float3s below).
//

#pragma once

#include <DirectXMath.h>
#include <cstdint>

__declspec(align(16)) struct LPVConstants
{
    DirectX::XMFLOAT4X4 ViewProj;          // camera view-projection
    DirectX::XMFLOAT4X4 InvViewProj;       // inverse camera view-proj (composite world-pos reconstruction)
    DirectX::XMFLOAT4X4 LightViewProj;     // sun orthographic view-proj (RSM camera)
    DirectX::XMFLOAT4X4 InvLightViewProj;  // inverse sun view-proj (injection world-pos reconstruction)

    DirectX::XMFLOAT3 CamPos;      float SunIntensity;
    DirectX::XMFLOAT3 SunDir;      float ShadowBias;     // SunDir = direction light travels (toward the scene)
    DirectX::XMFLOAT3 SunColor;    float IndirectScale;
    DirectX::XMFLOAT3 LpvMin;      float CellSize;       // grid min corner (world) and cell edge length

    int32_t LpvSize; int32_t RsmSize; int32_t DisplayMode; float InjectScale;
    float OcclusionAmp; int32_t UseGV; float Exposure; float Pad0;
};

// Display modes, shared with the composite pixel shader (gDisplayMode).
enum LPVDisplayMode
{
    kLPVDisplay_Final = 0,
    kLPVDisplay_Direct = 1,
    kLPVDisplay_Indirect = 2,
    kLPVDisplay_Albedo = 3,
    kLPVDisplay_Normal = 4,
    kLPVDisplay_Count
};

static const int kLPVGridSize = 32;    // 32^3 cells (single cascade)
static const int kRSMSize = 1024;      // 1024^2 reflective shadow map
