//
// VolumeColorBuffer.cpp
//

#include "pch.h"
#include "VolumeColorBuffer.h"
#include "GraphicsCore.h"

using namespace Graphics;

VolumeColorBuffer::VolumeColorBuffer()
    : m_Width(0), m_Height(0), m_Depth(0), m_Format(DXGI_FORMAT_UNKNOWN)
{
    m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
    m_UAVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
    m_RTVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
}

void VolumeColorBuffer::Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t depth, DXGI_FORMAT format)
{
    m_Width = width;
    m_Height = height;
    m_Depth = depth;
    m_Format = format;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = (UINT16)depth;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    m_UsageState = D3D12_RESOURCE_STATE_COMMON;
    ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc, m_UsageState, nullptr,
        MY_IID_PPV_ARGS(m_pResource.ReleaseAndGetAddressOf())));

    m_pResource->SetName(name.c_str());

    // SRV (Texture3D)
    m_SRVHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    g_Device->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_SRVHandle);

    // UAV (RWTexture3D) - all W slices
    m_UAVHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.FirstWSlice = 0;
    uavDesc.Texture3D.WSize = depth;
    g_Device->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, m_UAVHandle);

    // RTV (Texture3D) - all W slices, addressed by SV_RenderTargetArrayIndex
    m_RTVHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
    rtvDesc.Texture3D.FirstWSlice = 0;
    rtvDesc.Texture3D.WSize = depth;
    g_Device->CreateRenderTargetView(m_pResource.Get(), &rtvDesc, m_RTVHandle);
}
