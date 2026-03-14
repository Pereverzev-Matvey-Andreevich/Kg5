#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Utils.h"

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
    static const UINT RT_COUNT = 3;

    static const DXGI_FORMAT AlbedoFmt   = DXGI_FORMAT_R8G8B8A8_UNORM;
    static const DXGI_FORMAT NormalFmt   = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static const DXGI_FORMAT WorldPosFmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
    static const DXGI_FORMAT DepthFmt    = DXGI_FORMAT_D32_FLOAT;

    void Create(ID3D12Device* device, UINT width, UINT height);
    void Resize(ID3D12Device* device, UINT width, UINT height);

    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmd);
    void TransitionToShaderResource(ID3D12GraphicsCommandList* cmd);
    void Clear(ID3D12GraphicsCommandList* cmd);

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(UINT i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV()       const;
    ID3D12DescriptorHeap*       GetSRVHeap()   const { return srvHeap_.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVTable()  const;

    UINT GetWidth()  const { return width_; }
    UINT GetHeight() const { return height_; }

private:
    void CreateTextures(ID3D12Device* device);
    void CreateViews(ID3D12Device* device);

    UINT width_  = 0;
    UINT height_ = 0;

    ComPtr<ID3D12Resource> rts_[RT_COUNT];
    ComPtr<ID3D12Resource> depth_;

    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;

    UINT rtvDescSize_ = 0;
    UINT srvDescSize_ = 0;
};
