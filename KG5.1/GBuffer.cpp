#include "GBuffer.h"
#include <stdexcept>

void GBuffer::Create(ID3D12Device* device, UINT width, UINT height)
{
    width_  = width;
    height_ = height;

    rtvDescSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvDescSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = RT_COUNT;
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&rtvHeap_)));
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 1;
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&dsvHeap_)));
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = RT_COUNT;
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&srvHeap_)));
    }

    CreateTextures(device);
    CreateViews(device);
}

void GBuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
    width_  = width;
    height_ = height;

    for (UINT i = 0; i < RT_COUNT; ++i)
        rts_[i].Reset();
    depth_.Reset();

    CreateTextures(device);
    CreateViews(device);
}

void GBuffer::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmd)
{
    D3D12_RESOURCE_BARRIER barriers[RT_COUNT] = {};
    for (UINT i = 0; i < RT_COUNT; ++i)
    {
        barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource   = rts_[i].Get();
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmd->ResourceBarrier(RT_COUNT, barriers);
}

void GBuffer::TransitionToShaderResource(ID3D12GraphicsCommandList* cmd)
{
    D3D12_RESOURCE_BARRIER barriers[RT_COUNT] = {};
    for (UINT i = 0; i < RT_COUNT; ++i)
    {
        barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource   = rts_[i].Get();
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmd->ResourceBarrier(RT_COUNT, barriers);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmd)
{
    const float black[4]      = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float worldClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    cmd->ClearRenderTargetView(GetRTV(0), black,      0, nullptr);
    cmd->ClearRenderTargetView(GetRTV(1), black,      0, nullptr);
    cmd->ClearRenderTargetView(GetRTV(2), worldClear, 0, nullptr);
    cmd->ClearDepthStencilView(GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetRTV(UINT i) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += i * rtvDescSize_;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetDSV() const
{
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::GetSRVTable() const
{
    return srvHeap_->GetGPUDescriptorHandleForHeapStart();
}

void GBuffer::CreateTextures(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    DXGI_FORMAT formats[RT_COUNT] = { AlbedoFmt, NormalFmt, WorldPosFmt };

    for (UINT i = 0; i < RT_COUNT; ++i)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width_;
        desc.Height           = height_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = formats[i];
        desc.SampleDesc       = { 1, 0 };
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = formats[i];

        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
            IID_PPV_ARGS(&rts_[i])));
    }

    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width_;
        desc.Height           = height_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DepthFmt;
        desc.SampleDesc       = { 1, 0 };
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format             = DepthFmt;
        clear.DepthStencil.Depth = 1.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
            IID_PPV_ARGS(&depth_)));
    }
}

void GBuffer::CreateViews(ID3D12Device* device)
{
    DXGI_FORMAT formats[RT_COUNT] = { AlbedoFmt, NormalFmt, WorldPosFmt };

    for (UINT i = 0; i < RT_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = formats[i];
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * rtvDescSize_;
        device->CreateRenderTargetView(rts_[i].Get(), &rtvDesc, h);
    }

    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DepthFmt;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            depth_.Get(), &dsvDesc,
            dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    }

    for (UINT i = 0; i < RT_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = formats[i];
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * srvDescSize_;
        device->CreateShaderResourceView(rts_[i].Get(), &srvDesc, h);
    }
}
