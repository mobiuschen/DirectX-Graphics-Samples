//
// VolumeColorBuffer.h
//
// A 3D (volume) texture with SRV, UAV and a Texture3D RTV.  MiniEngine's ColorBuffer
// only supports 2D / 2D-array resources, but the LPV grids need true Texture3D:
//   - RTV (FirstWSlice=0, WSize=depth) for injection via SV_RenderTargetArrayIndex,
//   - UAV for compute propagation,
//   - SRV for trilinear sampling during the final gather.
//

#pragma once

#include "GpuResource.h"

class VolumeColorBuffer : public GpuResource
{
public:
    VolumeColorBuffer();

    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t depth, DXGI_FORMAT format);

    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTV() const { return m_RTVHandle; }

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    uint32_t GetDepth() const { return m_Depth; }
    DXGI_FORMAT GetFormat() const { return m_Format; }

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_RTVHandle;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_Depth;
    DXGI_FORMAT m_Format;
};
