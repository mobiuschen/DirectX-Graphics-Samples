//*********************************************************
//
// Light Propagation Volumes (LPV) dynamic global illumination, built on MiniEngine.
//
// The direct-lit scene (sun + shadows + SSAO) is produced by MiniEngine's Sponza
// renderer; this sample layers fully dynamic one-bounce indirect lighting on top with
// these extra GPU passes each frame:
//   1. Albedo G-buffer   - per-pixel albedo for the indirect * albedo term
//   2. Reflective Shadow Map (RSM) - flux/normal/depth from the sun
//   3. LPV + Geometry-Volume injection (SH splat via a GS slice-routing pass)
//   4. Propagation        - iterative compute spread of SH flux (with GV occlusion)
//   5. Composite          - add indirect * albedo to the HDR scene color
//
// Technique: Kaplanyan & Dachsbacher, "Cascaded Light Propagation Volumes for
// Real-Time Indirect Illumination", I3D 2010.
//
//*********************************************************

#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "CommandContext.h"
#include "Display.h"
#include "GameInput.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "MotionBlur.h"
#include "TemporalEffects.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "GraphicsCommon.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "EngineTuning.h"
#include "SponzaRenderer.h"
#include "ModelH3D.h"
#include "Renderer.h"

#include "LPVConstants.h"
#include "VolumeColorBuffer.h"

#include "CompiledShaders/RSMVS.h"
#include "CompiledShaders/RSMPS.h"
#include "CompiledShaders/AlbedoVS.h"
#include "CompiledShaders/AlbedoPS.h"
#include "CompiledShaders/InjectVS.h"
#include "CompiledShaders/InjectGS.h"
#include "CompiledShaders/InjectPS.h"
#include "CompiledShaders/GVInjectVS.h"
#include "CompiledShaders/GVInjectGS.h"
#include "CompiledShaders/GVInjectPS.h"
#include "CompiledShaders/PropagateCS.h"
#include "CompiledShaders/CompositeVS.h"
#include "CompiledShaders/CompositePS.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

namespace
{
    const char* s_DebugViewNames[] = { "Final", "Direct", "Indirect", "Albedo", "Normal" };
    EnumVar s_DebugView("LPV/Debug View", kLPVDisplay_Final, kLPVDisplay_Count, s_DebugViewNames);
    NumVar  s_Iterations("LPV/Propagation Steps", 12.0f, 0.0f, 24.0f, 1.0f);
    NumVar  s_InjectScale("LPV/Inject Scale", 1.0f, 0.0f, 8.0f, 0.1f);
    NumVar  s_IndirectScale("LPV/Indirect Scale", 1.5f, 0.0f, 8.0f, 0.1f);
    NumVar  s_OcclusionAmp("LPV/Occlusion Amp", 1.0f, 0.0f, 4.0f, 0.1f);
    BoolVar s_UseGV("LPV/Use Geometry Volume", true);

    const DXGI_FORMAT kVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT kRsmFluxFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT kRsmNormalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    const DXGI_FORMAT kRsmDepthFormat = DXGI_FORMAT_D32_FLOAT;
    const DXGI_FORMAT kAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    const UINT c_NumCameraPositions = 5;
}

class RTRLightPropagationVolumes : public GameCore::IGameApp
{
public:
    RTRLightPropagationVolumes() {}

    virtual void Startup(void) override;
    virtual void Cleanup(void) override;
    virtual void Update(float deltaT) override;
    virtual void RenderScene(void) override;
    virtual void RenderUI(GraphicsContext& context) override;

private:
    void CreateGridFromModel(const ModelH3D& model);
    void CreateResources();
    void CreateRootSignatures();
    void CreatePSOs();

    LPVConstants BuildConstants();
    void RenderModelGeometry(GraphicsContext& ctx, const ModelH3D& model);

    void RenderAlbedo(GraphicsContext& ctx, const LPVConstants& cb);
    void RenderRSM(GraphicsContext& ctx, const LPVConstants& cb);
    void Inject(GraphicsContext& ctx, const LPVConstants& cb);
    bool Propagate(GraphicsContext& ctx, const LPVConstants& cb);  // returns true if result is in accA
    void Composite(GraphicsContext& ctx, const LPVConstants& cb, bool resultInA);

    void SetCameraToPredefinedPosition(int pos);

    Camera m_Camera;
    std::unique_ptr<FlyingFPSCamera> m_CameraController;
    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    // LPV grid (single 32^3 cascade fit to the scene bounds)
    Vector3 m_GridMin = Vector3(kZero);
    float m_CellSize = 1.0f;

    // Reflective shadow map + albedo G-buffer
    ColorBuffer m_RsmFlux;
    ColorBuffer m_RsmNormal;
    DepthBuffer m_RsmDepth;
    ColorBuffer m_Albedo;

    // LPV volumes: 3 injected (R/G/B), 1 geometry volume, ping-pong step + accumulator
    VolumeColorBuffer m_Inject[3];
    VolumeColorBuffer m_GV;
    VolumeColorBuffer m_StepA[3];
    VolumeColorBuffer m_StepB[3];
    VolumeColorBuffer m_AccA[3];
    VolumeColorBuffer m_AccB[3];

    RootSignature m_GeoRS;        // RSM + albedo geometry passes
    RootSignature m_InjectRS;     // LPV + GV injection
    RootSignature m_PropRS;       // propagation compute
    RootSignature m_CompositeRS;  // fullscreen composite

    GraphicsPSO m_RsmPSO{ L"LPV: RSM" };
    GraphicsPSO m_AlbedoPSO{ L"LPV: Albedo" };
    GraphicsPSO m_InjectPSO{ L"LPV: Inject" };
    GraphicsPSO m_GvPSO{ L"LPV: GV Inject" };
    ComputePSO  m_PropPSO{ L"LPV: Propagate" };
    GraphicsPSO m_CompositeAddPSO{ L"LPV: Composite (additive)" };
    GraphicsPSO m_CompositeOpaquePSO{ L"LPV: Composite (opaque)" };

    std::vector<uint8_t> m_MaterialIsCutout;

    struct CameraPosition { Vector3 position; float heading; float pitch; };
    CameraPosition m_CameraPosArray[c_NumCameraPositions];
    UINT m_CameraPosCurrent = 0;
};

CREATE_APPLICATION(RTRLightPropagationVolumes)

// ---------------------------------------------------------------------------

void RTRLightPropagationVolumes::Startup(void)
{
    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = false;
    PostEffects::EnableHDR = false;
    PostEffects::EnableAdaptation = false;
    SSAO::Enable = true;

    Renderer::Initialize();
    Sponza::Startup(m_Camera);

    m_Camera.SetZRange(1.0f, 10000.0f);
    m_CameraController.reset(new FlyingFPSCamera(m_Camera, Vector3(kYUnitVector)));

    const ModelH3D& model = Sponza::GetModel();

    // Flag alpha-masked materials (foliage/chains) the same way SponzaRenderer does.
    m_MaterialIsCutout.resize(model.GetMaterialCount());
    for (uint32_t i = 0; i < model.GetMaterialCount(); ++i)
    {
        std::string path = model.GetMaterial(i).texDiffusePath;
        bool cutout = path.find("thorn") != std::string::npos
            || path.find("plant") != std::string::npos
            || path.find("chain") != std::string::npos;
        m_MaterialIsCutout[i] = cutout ? 1 : 0;
    }

    CreateGridFromModel(model);
    CreateResources();
    CreateRootSignatures();
    CreatePSOs();

    // Predefined camera positions (same vantage points as the raytracing sample).
    m_CameraPosCurrent = 0;
    m_CameraPosArray[0] = { Vector3(-1100.0f, 170.0f, -30.0f), 1.5707f, 0.0f };
    m_CameraPosArray[1] = { Vector3(299.0f, 208.0f, -202.0f), -3.1111f, 0.5953f };
    m_CameraPosArray[2] = { Vector3(-1237.61f, 80.60f, -26.02f), -1.5707f, 0.268f };
    m_CameraPosArray[3] = { Vector3(-977.90f, 595.05f, -194.97f), -2.077f, -0.450f };
    m_CameraPosArray[4] = { Vector3(-1463.0f, 600.0f, 394.52f), -1.236f, 0.0f };
}

void RTRLightPropagationVolumes::Cleanup(void)
{
    Sponza::Cleanup();
    Renderer::Shutdown();
}

void RTRLightPropagationVolumes::CreateGridFromModel(const ModelH3D& model)
{
    AxisAlignedBox bb = model.GetBoundingBox();
    Vector3 dims = bb.GetMax() - bb.GetMin();
    float maxExt = Max(Max((float)dims.GetX(), (float)dims.GetY()), (float)dims.GetZ());
    m_CellSize = maxExt / (kLPVGridSize - 2);
    Vector3 center = (bb.GetMax() + bb.GetMin()) * 0.5f;
    m_GridMin = center - Vector3(0.5f * kLPVGridSize * m_CellSize);
}

void RTRLightPropagationVolumes::CreateResources(void)
{
    uint32_t w = g_SceneColorBuffer.GetWidth();
    uint32_t h = g_SceneColorBuffer.GetHeight();

    m_RsmFlux.Create(L"LPV RSM Flux", kRSMSize, kRSMSize, 1, kRsmFluxFormat);
    m_RsmNormal.Create(L"LPV RSM Normal", kRSMSize, kRSMSize, 1, kRsmNormalFormat);
    m_RsmDepth = DepthBuffer(1.0f, 0);    // standard-Z: clear to the far plane
    m_RsmDepth.Create(L"LPV RSM Depth", kRSMSize, kRSMSize, kRsmDepthFormat);
    m_Albedo.Create(L"LPV Albedo", w, h, 1, kAlbedoFormat);

    for (int c = 0; c < 3; ++c)
    {
        m_Inject[c].Create(L"LPV Inject", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
        m_StepA[c].Create(L"LPV StepA", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
        m_StepB[c].Create(L"LPV StepB", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
        m_AccA[c].Create(L"LPV AccA", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
        m_AccB[c].Create(L"LPV AccB", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
    }
    m_GV.Create(L"LPV Geometry Volume", kLPVGridSize, kLPVGridSize, kLPVGridSize, kVolumeFormat);
}

void RTRLightPropagationVolumes::CreateRootSignatures(void)
{
    // Geometry passes (RSM, albedo): b0 frame CB, b1 mat flags, t0 diffuse, s0 aniso
    m_GeoRS.Reset(3, 1);
    m_GeoRS[0].InitAsConstantBuffer(0);
    m_GeoRS[1].InitAsConstants(1, 1);
    m_GeoRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    m_GeoRS.InitStaticSampler(0, SamplerAnisoWrapDesc);
    m_GeoRS.Finalize(L"LPV Geo RS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // Injection: b0 frame CB, t1-t3 RSM (flux/normal/depth)
    m_InjectRS.Reset(2, 0);
    m_InjectRS[0].InitAsConstantBuffer(0);
    m_InjectRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    m_InjectRS.Finalize(L"LPV Inject RS");

    // Propagation: b0 frame CB, b1 first-iteration, t0-t6 SRVs, u0-u5 UAVs, s1 linear clamp
    m_PropRS.Reset(4, 1);
    m_PropRS[0].InitAsConstantBuffer(0);
    m_PropRS[1].InitAsConstants(1, 1);
    m_PropRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 7);
    m_PropRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 6);
    m_PropRS.InitStaticSampler(1, SamplerLinearClampDesc);
    m_PropRS.Finalize(L"LPV Propagate RS");

    // Composite: b0 frame CB, t0-t5 (depth/normal/albedo/lpvR/lpvG/lpvB), s1 linear clamp
    m_CompositeRS.Reset(2, 1);
    m_CompositeRS[0].InitAsConstantBuffer(0);
    m_CompositeRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6);
    m_CompositeRS.InitStaticSampler(1, SamplerLinearClampDesc);
    m_CompositeRS.Finalize(L"LPV Composite RS");
}

void RTRLightPropagationVolumes::CreatePSOs(void)
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Standard-Z depth state (matches the RSM clear-to-1 convention).
    D3D12_DEPTH_STENCIL_DESC depthLess = {};
    depthLess.DepthEnable = TRUE;
    depthLess.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthLess.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // MAX blend for the geometry volume (avoids double-counting overlapping surfels).
    D3D12_BLEND_DESC blendMax = BlendDisable;
    blendMax.RenderTarget[0].BlendEnable = TRUE;
    blendMax.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendMax.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendMax.RenderTarget[0].BlendOp = D3D12_BLEND_OP_MAX;
    blendMax.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendMax.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendMax.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_MAX;

    // RSM: two-sided MRT (flux + normal) from the sun.
    DXGI_FORMAT rsmFmts[2] = { kRsmFluxFormat, kRsmNormalFormat };
    m_RsmPSO.SetRootSignature(m_GeoRS);
    m_RsmPSO.SetRasterizerState(RasterizerTwoSided);
    m_RsmPSO.SetBlendState(BlendDisable);
    m_RsmPSO.SetDepthStencilState(depthLess);
    m_RsmPSO.SetInputLayout(_countof(inputLayout), inputLayout);
    m_RsmPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_RsmPSO.SetRenderTargetFormats(2, rsmFmts, kRsmDepthFormat);
    m_RsmPSO.SetVertexShader(g_pRSMVS, sizeof(g_pRSMVS));
    m_RsmPSO.SetPixelShader(g_pRSMPS, sizeof(g_pRSMPS));
    m_RsmPSO.Finalize();

    // Albedo G-buffer: depth-equal against the populated scene depth.
    m_AlbedoPSO.SetRootSignature(m_GeoRS);
    m_AlbedoPSO.SetRasterizerState(RasterizerDefault);
    m_AlbedoPSO.SetBlendState(BlendDisable);
    m_AlbedoPSO.SetDepthStencilState(DepthStateTestEqual);
    m_AlbedoPSO.SetInputLayout(_countof(inputLayout), inputLayout);
    m_AlbedoPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_AlbedoPSO.SetRenderTargetFormats(1, &kAlbedoFormat, g_SceneDepthBuffer.GetFormat());
    m_AlbedoPSO.SetVertexShader(g_pAlbedoVS, sizeof(g_pAlbedoVS));
    m_AlbedoPSO.SetPixelShader(g_pAlbedoPS, sizeof(g_pAlbedoPS));
    m_AlbedoPSO.Finalize();

    // LPV injection: point cloud, GS slice routing, additive into 3 SH volumes.
    DXGI_FORMAT injFmts[3] = { kVolumeFormat, kVolumeFormat, kVolumeFormat };
    m_InjectPSO.SetRootSignature(m_InjectRS);
    m_InjectPSO.SetRasterizerState(RasterizerTwoSided);
    m_InjectPSO.SetBlendState(BlendAdditive);
    m_InjectPSO.SetDepthStencilState(DepthStateDisabled);
    m_InjectPSO.SetInputLayout(0, nullptr);
    m_InjectPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
    m_InjectPSO.SetRenderTargetFormats(3, injFmts, DXGI_FORMAT_UNKNOWN);
    m_InjectPSO.SetVertexShader(g_pInjectVS, sizeof(g_pInjectVS));
    m_InjectPSO.SetGeometryShader(g_pInjectGS, sizeof(g_pInjectGS));
    m_InjectPSO.SetPixelShader(g_pInjectPS, sizeof(g_pInjectPS));
    m_InjectPSO.Finalize();

    // Geometry-volume injection: point cloud, MAX blend into a single SH volume.
    m_GvPSO.SetRootSignature(m_InjectRS);
    m_GvPSO.SetRasterizerState(RasterizerTwoSided);
    m_GvPSO.SetBlendState(blendMax);
    m_GvPSO.SetDepthStencilState(DepthStateDisabled);
    m_GvPSO.SetInputLayout(0, nullptr);
    m_GvPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
    m_GvPSO.SetRenderTargetFormats(1, &kVolumeFormat, DXGI_FORMAT_UNKNOWN);
    m_GvPSO.SetVertexShader(g_pGVInjectVS, sizeof(g_pGVInjectVS));
    m_GvPSO.SetGeometryShader(g_pGVInjectGS, sizeof(g_pGVInjectGS));
    m_GvPSO.SetPixelShader(g_pGVInjectPS, sizeof(g_pGVInjectPS));
    m_GvPSO.Finalize();

    m_PropPSO.SetRootSignature(m_PropRS);
    m_PropPSO.SetComputeShader(g_pPropagateCS, sizeof(g_pPropagateCS));
    m_PropPSO.Finalize();

    // Composite: fullscreen triangle adding indirect * albedo (or a debug view).
    DXGI_FORMAT sceneFmt = g_SceneColorBuffer.GetFormat();
    m_CompositeAddPSO.SetRootSignature(m_CompositeRS);
    m_CompositeAddPSO.SetRasterizerState(RasterizerTwoSided);
    m_CompositeAddPSO.SetBlendState(BlendAdditive);
    m_CompositeAddPSO.SetDepthStencilState(DepthStateDisabled);
    m_CompositeAddPSO.SetInputLayout(0, nullptr);
    m_CompositeAddPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_CompositeAddPSO.SetRenderTargetFormats(1, &sceneFmt, DXGI_FORMAT_UNKNOWN);
    m_CompositeAddPSO.SetVertexShader(g_pCompositeVS, sizeof(g_pCompositeVS));
    m_CompositeAddPSO.SetPixelShader(g_pCompositePS, sizeof(g_pCompositePS));
    m_CompositeAddPSO.Finalize();

    m_CompositeOpaquePSO = m_CompositeAddPSO;
    m_CompositeOpaquePSO.SetBlendState(BlendDisable);
    m_CompositeOpaquePSO.Finalize();
}

// ---------------------------------------------------------------------------

void RTRLightPropagationVolumes::Update(float deltaT)
{
    ScopedTimer _prof(L"Update State");

    if (GameInput::IsFirstPressed(GameInput::kKey_left))
    {
        m_CameraPosCurrent = (m_CameraPosCurrent + c_NumCameraPositions - 1) % c_NumCameraPositions;
        SetCameraToPredefinedPosition(m_CameraPosCurrent);
    }
    else if (GameInput::IsFirstPressed(GameInput::kKey_right))
    {
        m_CameraPosCurrent = (m_CameraPosCurrent + 1) % c_NumCameraPositions;
        SetCameraToPredefinedPosition(m_CameraPosCurrent);
    }

    m_CameraController->Update(deltaT);

    m_MainViewport.TopLeftX = 0.0f;
    m_MainViewport.TopLeftY = 0.0f;
    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void RTRLightPropagationVolumes::SetCameraToPredefinedPosition(int pos)
{
    if (pos < 0 || pos >= (int)c_NumCameraPositions)
        return;
    m_CameraController->SetHeadingPitchAndPosition(
        m_CameraPosArray[pos].heading, m_CameraPosArray[pos].pitch, m_CameraPosArray[pos].position);
}

LPVConstants RTRLightPropagationVolumes::BuildConstants(void)
{
    using namespace DirectX;

    LPVConstants cb = {};

    // Camera matrices (MiniEngine convention: mul(matrix, vector) in HLSL).
    XMStoreFloat4x4(&cb.ViewProj, (XMMATRIX)m_Camera.GetViewProjMatrix());
    XMStoreFloat4x4(&cb.InvViewProj, (XMMATRIX)Invert(m_Camera.GetViewProjMatrix()));

    // Build a standard-Z orthographic light view-projection fit to the scene AABB,
    // looking along the sun's travel direction (GIPlayground convention).
    Vector3 toSun = Normalize(Sponza::m_SunDirection);           // points toward the sun
    XMVECTOR sd = XMVector3Normalize(XMVectorNegate(toSun));       // light travel direction
    AxisAlignedBox bb = Sponza::GetModel().GetBoundingBox();
    Vector3 centerV = (bb.GetMax() + bb.GetMin()) * 0.5f;
    XMVECTOR center = centerV;
    float radius = 0.5f * Length(bb.GetMax() - bb.GetMin());
    XMVECTOR up = (fabsf(XMVectorGetY(sd)) > 0.99f) ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0);
    XMMATRIX lview = XMMatrixLookToLH(XMVectorSubtract(center, XMVectorScale(sd, radius * 1.5f + 1.0f)), sd, up);

    Vector3 bbMin = bb.GetMin(), bbMax = bb.GetMax();
    XMFLOAT3 lsMin(1e30f, 1e30f, 1e30f), lsMax(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR corner = XMVectorSet(
            (i & 1) ? (float)bbMax.GetX() : (float)bbMin.GetX(),
            (i & 2) ? (float)bbMax.GetY() : (float)bbMin.GetY(),
            (i & 4) ? (float)bbMax.GetZ() : (float)bbMin.GetZ(), 1.0f);
        XMFLOAT3 l; XMStoreFloat3(&l, XMVector3TransformCoord(corner, lview));
        lsMin.x = Min(lsMin.x, l.x); lsMax.x = Max(lsMax.x, l.x);
        lsMin.y = Min(lsMin.y, l.y); lsMax.y = Max(lsMax.y, l.y);
        lsMin.z = Min(lsMin.z, l.z); lsMax.z = Max(lsMax.z, l.z);
    }
    XMMATRIX lproj = XMMatrixOrthographicOffCenterLH(lsMin.x, lsMax.x, lsMin.y, lsMax.y, lsMin.z - 1.0f, lsMax.z + 1.0f);
    XMMATRIX lvp = XMMatrixMultiply(lview, lproj);
    XMStoreFloat4x4(&cb.LightViewProj, lvp);
    XMStoreFloat4x4(&cb.InvLightViewProj, XMMatrixInverse(nullptr, lvp));

    float orthoW = lsMax.x - lsMin.x;
    float orthoH = lsMax.y - lsMin.y;

    XMStoreFloat3(&cb.CamPos, m_Camera.GetPosition());
    XMStoreFloat3(&cb.SunDir, XMVectorNegate(toSun));   // direction light travels
    cb.SunColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
    cb.SunIntensity = (float)Sponza::m_SunLightIntensity;
    cb.ShadowBias = 0.0f;
    cb.IndirectScale = (float)s_IndirectScale;
    XMStoreFloat3(&cb.LpvMin, m_GridMin);
    cb.CellSize = m_CellSize;
    cb.LpvSize = kLPVGridSize;
    cb.RsmSize = kRSMSize;
    cb.DisplayMode = (int)s_DebugView;

    // Normalize VPL flux against the texel-to-cell area ratio so the result is
    // independent of RSM resolution and cell size.
    float texelArea = (orthoW / kRSMSize) * (orthoH / kRSMSize);
    cb.InjectScale = (float)s_InjectScale * texelArea / (m_CellSize * m_CellSize);
    cb.OcclusionAmp = (float)s_OcclusionAmp;
    cb.UseGV = s_UseGV ? 1 : 0;
    cb.Exposure = 1.0f;
    return cb;
}

void RTRLightPropagationVolumes::RenderModelGeometry(GraphicsContext& ctx, const ModelH3D& model)
{
    ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.SetIndexBuffer(model.GetIndexBuffer());
    ctx.SetVertexBuffer(0, model.GetVertexBuffer());

    uint32_t stride = model.GetVertexStride();
    uint32_t curMat = 0xFFFFFFFFu;
    for (uint32_t m = 0; m < model.GetMeshCount(); ++m)
    {
        const ModelH3D::Mesh& mesh = model.GetMesh(m);
        if (mesh.materialIndex != curMat)
        {
            curMat = mesh.materialIndex;
            ctx.SetConstants(1, m_MaterialIsCutout[curMat] ? 1u : 0u);
            ctx.SetDynamicDescriptor(2, 0, model.GetMaterialTextures(curMat)[0].GetSRV());
        }
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / stride;
        ctx.DrawIndexed(mesh.indexCount, startIndex, baseVertex);
    }
}

void RTRLightPropagationVolumes::RenderAlbedo(GraphicsContext& ctx, const LPVConstants& cb)
{
    ScopedTimer _prof(L"LPV Albedo G-buffer", ctx);

    ctx.TransitionResource(m_Albedo, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    ctx.ClearColor(m_Albedo);
    ctx.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

    ctx.SetRootSignature(m_GeoRS);
    ctx.SetPipelineState(m_AlbedoPSO);
    ctx.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
    ctx.SetRenderTarget(m_Albedo.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    ctx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
    RenderModelGeometry(ctx, Sponza::GetModel());
}

void RTRLightPropagationVolumes::RenderRSM(GraphicsContext& ctx, const LPVConstants& cb)
{
    ScopedTimer _prof(L"LPV Reflective Shadow Map", ctx);

    ctx.TransitionResource(m_RsmFlux, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ctx.TransitionResource(m_RsmNormal, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ctx.TransitionResource(m_RsmDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    ctx.ClearColor(m_RsmFlux);
    ctx.ClearColor(m_RsmNormal);
    ctx.ClearDepth(m_RsmDepth);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { m_RsmFlux.GetRTV(), m_RsmNormal.GetRTV() };
    ctx.SetRootSignature(m_GeoRS);
    ctx.SetPipelineState(m_RsmPSO);
    ctx.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
    ctx.SetRenderTargets(2, rtvs, m_RsmDepth.GetDSV());
    ctx.SetViewportAndScissor(0, 0, kRSMSize, kRSMSize);
    RenderModelGeometry(ctx, Sponza::GetModel());
}

void RTRLightPropagationVolumes::Inject(GraphicsContext& ctx, const LPVConstants& cb)
{
    ScopedTimer _prof(L"LPV Injection", ctx);

    // RSM textures become shader inputs (read in the vertex stage).
    ctx.TransitionResource(m_RsmFlux, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ctx.TransitionResource(m_RsmNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ctx.TransitionResource(m_RsmDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12_CPU_DESCRIPTOR_HANDLE rsmSrvs[3] = { m_RsmFlux.GetSRV(), m_RsmNormal.GetSRV(), m_RsmDepth.GetDepthSRV() };

    // --- LPV injection (3 SH volumes, additive) ---
    for (int c = 0; c < 3; ++c)
        ctx.TransitionResource(m_Inject[c], D3D12_RESOURCE_STATE_RENDER_TARGET);
    ctx.FlushResourceBarriers();
    const float zero[4] = { 0, 0, 0, 0 };
    for (int c = 0; c < 3; ++c)
        ctx.GetCommandList()->ClearRenderTargetView(m_Inject[c].GetRTV(), zero, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE injRtvs[3] = { m_Inject[0].GetRTV(), m_Inject[1].GetRTV(), m_Inject[2].GetRTV() };
    ctx.SetRootSignature(m_InjectRS);
    ctx.SetPipelineState(m_InjectPSO);
    ctx.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
    ctx.SetDynamicDescriptors(1, 0, 3, rsmSrvs);
    ctx.SetRenderTargets(3, injRtvs);
    ctx.SetViewportAndScissor(0, 0, kLPVGridSize, kLPVGridSize);
    ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx.Draw(kRSMSize * kRSMSize);

    // --- Geometry volume injection (1 SH volume, MAX blend) ---
    ctx.TransitionResource(m_GV, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    ctx.GetCommandList()->ClearRenderTargetView(m_GV.GetRTV(), zero, 0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE gvRtv = m_GV.GetRTV();
    ctx.SetPipelineState(m_GvPSO);
    ctx.SetDynamicDescriptors(1, 0, 3, rsmSrvs);
    ctx.SetRenderTargets(1, &gvRtv);
    ctx.SetViewportAndScissor(0, 0, kLPVGridSize, kLPVGridSize);
    ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx.Draw(kRSMSize * kRSMSize);
}

bool RTRLightPropagationVolumes::Propagate(GraphicsContext& ctx, const LPVConstants& cb)
{
    ScopedTimer _prof(L"LPV Propagation", ctx);

    // Seed the accumulator with the injected VPLs (accA = inject).
    for (int c = 0; c < 3; ++c)
    {
        ctx.TransitionResource(m_Inject[c], D3D12_RESOURCE_STATE_COPY_SOURCE);
        ctx.TransitionResource(m_AccA[c], D3D12_RESOURCE_STATE_COPY_DEST);
    }
    ctx.FlushResourceBarriers();
    for (int c = 0; c < 3; ++c)
        ctx.GetCommandList()->CopyResource(m_AccA[c].GetResource(), m_Inject[c].GetResource());

    // Geometry volume is read during propagation.
    ctx.TransitionResource(m_GV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    int iterations = (int)(float)s_Iterations;
    bool resultInA = true;   // 0 iterations => the seeded accA holds the result

    if (iterations > 0)
    {
        ComputeContext& cc = ctx.GetComputeContext();
        cc.SetRootSignature(m_PropRS);
        cc.SetPipelineState(m_PropPSO);

        for (int i = 0; i < iterations; ++i)
        {
            bool writeA = (i % 2 == 0);
            VolumeColorBuffer* wStep = writeA ? m_StepA : m_StepB;
            VolumeColorBuffer* wAcc = writeA ? m_AccB : m_AccA;
            VolumeColorBuffer* rStep = (i == 0) ? m_Inject : (writeA ? m_StepB : m_StepA);
            VolumeColorBuffer* rAcc = writeA ? m_AccA : m_AccB;

            for (int c = 0; c < 3; ++c)
            {
                cc.TransitionResource(rStep[c], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                cc.TransitionResource(rAcc[c], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                cc.TransitionResource(wStep[c], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cc.TransitionResource(wAcc[c], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE srvs[7] =
            {
                rStep[0].GetSRV(), rStep[1].GetSRV(), rStep[2].GetSRV(),
                rAcc[0].GetSRV(),  rAcc[1].GetSRV(),  rAcc[2].GetSRV(),
                m_GV.GetSRV()
            };
            D3D12_CPU_DESCRIPTOR_HANDLE uavs[6] =
            {
                wStep[0].GetUAV(), wStep[1].GetUAV(), wStep[2].GetUAV(),
                wAcc[0].GetUAV(),  wAcc[1].GetUAV(),  wAcc[2].GetUAV()
            };

            cc.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
            cc.SetConstants(1, (UINT)(i == 0 ? 1 : 0));
            cc.SetDynamicDescriptors(2, 0, 7, srvs);
            cc.SetDynamicDescriptors(3, 0, 6, uavs);
            cc.Dispatch(kLPVGridSize / 4, kLPVGridSize / 4, kLPVGridSize / 4);

            resultInA = !writeA;   // result lives in the accumulator we just wrote
        }
    }

    return resultInA;
}

void RTRLightPropagationVolumes::Composite(GraphicsContext& ctx, const LPVConstants& cb, bool resultInA)
{
    if (s_DebugView == kLPVDisplay_Direct)
        return;   // direct-only: leave the scene color as the Sponza render produced it

    ScopedTimer _prof(L"LPV Composite", ctx);

    VolumeColorBuffer* lpv = resultInA ? m_AccA : m_AccB;
    for (int c = 0; c < 3; ++c)
        ctx.TransitionResource(lpv[c], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ctx.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.TransitionResource(m_Albedo, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

    D3D12_CPU_DESCRIPTOR_HANDLE srvs[6] =
    {
        g_SceneDepthBuffer.GetDepthSRV(), g_SceneNormalBuffer.GetSRV(), m_Albedo.GetSRV(),
        lpv[0].GetSRV(), lpv[1].GetSRV(), lpv[2].GetSRV()
    };

    ctx.SetRootSignature(m_CompositeRS);
    ctx.SetPipelineState(s_DebugView == kLPVDisplay_Final ? m_CompositeAddPSO : m_CompositeOpaquePSO);
    ctx.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
    ctx.SetDynamicDescriptors(1, 0, 6, srvs);
    ctx.SetRenderTarget(g_SceneColorBuffer.GetRTV());
    ctx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
    ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.Draw(3);
}

void RTRLightPropagationVolumes::RenderScene(void)
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    // 1. Direct-lit scene (sun + shadows + SSAO) into g_SceneColorBuffer/Normal/Depth.
    Sponza::RenderScene(gfxContext, m_Camera, m_MainViewport, m_MainScissor, false, false);

    // 2-5. Layer LPV indirect lighting.
    LPVConstants cb = BuildConstants();
    RenderAlbedo(gfxContext, cb);
    RenderRSM(gfxContext, cb);
    Inject(gfxContext, cb);
    bool resultInA = Propagate(gfxContext, cb);
    Composite(gfxContext, cb, resultInA);

    gfxContext.Finish();
}

void RTRLightPropagationVolumes::RenderUI(GraphicsContext& context)
{
    TextContext text(context);
    text.Begin();
    text.DrawString("Light Propagation Volumes - press Back/~ for the LPV tuning menu");
    text.End();
}
