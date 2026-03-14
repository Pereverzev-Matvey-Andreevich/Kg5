#include "RenderingSystem.h"
#include "Utils.h"
#include <cassert>
#include <cmath>

RenderingSystem::RenderingSystem(HWND hwnd, int width, int height)
    : hwnd_(hwnd), width_(width), height_(height)
{
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        fenceValues_[i] = 0;
}

RenderingSystem::~RenderingSystem()
{
    if (commandQueue_ && fence_ && fenceEvent_)
        try { WaitForGPU(); } catch (...) {}

    if (geomCB_  && geomCBMapped_)  geomCB_->Unmap(0, nullptr);
    if (lightCB_ && lightCBMapped_) lightCB_->Unmap(0, nullptr);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

bool RenderingSystem::Initialize()
{
    try
    {
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain();
        CreateDescriptorHeaps();
        CreateRenderTargetViews();
        CreateCommandObjects();
        CreateFence();
        gbuffer_.Create(device_.Get(), static_cast<UINT>(width_), static_cast<UINT>(height_));
        CreateGeometryPassPipeline();
        CreateLightingPassPipeline();
        CreateConstantBuffers();
        CreateSphereBuffers();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(hwnd_, e.what(), "RenderingSystem Init Error", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void RenderingSystem::CreateDevice()
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
#endif
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
        factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)))) return;
    }
    ThrowIfFailed(E_FAIL, "No D3D12 device found");
}

void RenderingSystem::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue_)));
}

void RenderingSystem::CreateSwapChain()
{
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = FRAME_COUNT;
    desc.Width       = static_cast<UINT>(width_);
    desc.Height      = static_cast<UINT>(height_);
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc  = { 1, 0 };
    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue_.Get(), hwnd_, &desc, nullptr, nullptr, &sc1));
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    ThrowIfFailed(sc1.As(&swapChain_));
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void RenderingSystem::CreateDescriptorHeaps()
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = FRAME_COUNT;
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(device_->CreateDescriptorHeap(&d, IID_PPV_ARGS(&rtvHeap_)));
        rtvDescSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 2;
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device_->CreateDescriptorHeap(&d, IID_PPV_ARGS(&srvHeap_)));
        srvDescSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void RenderingSystem::CreateRenderTargetViews()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; ++i)
    {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])));
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, handle);
        handle.ptr += rtvDescSize_;
    }
}

void RenderingSystem::CreateCommandObjects()
{
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        ThrowIfFailed(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators_[i])));
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_)));
    commandList_->Close();
}

void RenderingSystem::CreateFence()
{
    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    fenceValues_[0] = 1;
    fenceValues_[1] = 1;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
}

void RenderingSystem::CreateGeometryPassPipeline()
{
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 2;
    srvRange.BaseShaderRegister = 0;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&geomRootSig_)));

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring shaderPath(exePath);
    shaderPath = shaderPath.substr(0, shaderPath.find_last_of(L"\\/") + 1) + L"DeferredShaders.hlsl";

    auto vs = CompileShader(shaderPath, "GeometryVS", "vs_5_0");
    auto ps = CompileShader(shaderPath, "GeometryPS", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,   0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC raster = {};
    raster.FillMode        = D3D12_FILL_MODE_SOLID;
    raster.CullMode        = D3D12_CULL_MODE_BACK;
    raster.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_BLEND_DESC blend = {};
    for (UINT i = 0; i < GBuffer::RT_COUNT; ++i)
        blend.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout           = { layout, _countof(layout) };
    psoDesc.pRootSignature        = geomRootSig_.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState       = raster;
    psoDesc.BlendState            = blend;
    psoDesc.DepthStencilState     = ds;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = GBuffer::RT_COUNT;
    psoDesc.RTVFormats[0]         = GBuffer::AlbedoFmt;
    psoDesc.RTVFormats[1]         = GBuffer::NormalFmt;
    psoDesc.RTVFormats[2]         = GBuffer::WorldPosFmt;
    psoDesc.DSVFormat             = GBuffer::DepthFmt;
    psoDesc.SampleDesc            = { 1, 0 };
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&geomPSO_)));
}

void RenderingSystem::CreateLightingPassPipeline()
{
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE gbufRange = {};
    gbufRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gbufRange.NumDescriptors     = GBuffer::RT_COUNT;
    gbufRange.BaseShaderRegister = 0;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &gbufRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&lightRootSig_)));

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring shaderPath(exePath);
    shaderPath = shaderPath.substr(0, shaderPath.find_last_of(L"\\/") + 1) + L"DeferredShaders.hlsl";

    auto vs = CompileShader(shaderPath, "LightingVS", "vs_5_0");
    auto ps = CompileShader(shaderPath, "LightingPS", "ps_5_0");

    D3D12_DEPTH_STENCIL_DESC dsDisabled = {};
    dsDisabled.DepthEnable = FALSE;

    D3D12_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = lightRootSig_.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState       = raster;
    psoDesc.BlendState            = blend;
    psoDesc.DepthStencilState     = dsDisabled;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc            = { 1, 0 };
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&lightPSO_)));
}

void RenderingSystem::CreateConstantBuffers()
{
    auto makeCB = [&](UINT64 size, ComPtr<ID3D12Resource>& res, void** mapped)
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = size;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));
        D3D12_RANGE r = { 0, 0 };
        ThrowIfFailed(res->Map(0, &r, mapped));
    };
    makeCB(sizeof(GeometryCBData) * MAX_GEOM_DRAWS, geomCB_,  reinterpret_cast<void**>(&geomCBMapped_));
    makeCB(sizeof(LightingCBData), lightCB_, reinterpret_cast<void**>(&lightCBMapped_));
}

void RenderingSystem::CreateSphereBuffers()
{
    const int rings   = 12;
    const int sectors = 12;
    const float radius = 1.0f;
    const float pi     = 3.14159265f;

    std::vector<Vertex> verts;
    std::vector<UINT>   idxs;

    for (int r = 0; r <= rings; ++r)
    {
        float phi = pi * r / rings;
        for (int s = 0; s <= sectors; ++s)
        {
            float theta = 2.0f * pi * s / sectors;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);

            Vertex v;
            v.Position = XMFLOAT3(x * radius, y * radius, z * radius);
            v.Normal   = XMFLOAT3(x, y, z);
            v.Color    = XMFLOAT4(0.8f, 0.0f, 1.0f, 0.0f);
            v.TexCoord = XMFLOAT2((float)s / sectors, (float)r / rings);
            verts.push_back(v);
        }
    }

    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            UINT a = r * (sectors + 1) + s;
            UINT b = a + (sectors + 1);
            idxs.push_back(a);
            idxs.push_back(b);
            idxs.push_back(a + 1);
            idxs.push_back(a + 1);
            idxs.push_back(b);
            idxs.push_back(b + 1);
        }
    }

    sphereIndexCount_ = static_cast<UINT>(idxs.size());

    UINT64 vbSize = verts.size() * sizeof(Vertex);
    UINT64 ibSize = idxs.size()  * sizeof(UINT);

    MakeUploadBuffer(verts.data(), vbSize, sphereVB_);
    MakeUploadBuffer(idxs.data(),  ibSize, sphereIB_);

    sphereVBView_.BufferLocation = sphereVB_->GetGPUVirtualAddress();
    sphereVBView_.SizeInBytes    = static_cast<UINT>(vbSize);
    sphereVBView_.StrideInBytes  = sizeof(Vertex);

    sphereIBView_.BufferLocation = sphereIB_->GetGPUVirtualAddress();
    sphereIBView_.SizeInBytes    = static_cast<UINT>(ibSize);
    sphereIBView_.Format         = DXGI_FORMAT_R32_UINT;
}

void RenderingSystem::MakeUploadBuffer(const void* data, UINT64 byteSize,
                                        ComPtr<ID3D12Resource>& buf)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = byteSize;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc       = { 1, 0 };
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ThrowIfFailed(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf)));
    void* mapped = nullptr;
    D3D12_RANGE r = { 0, 0 };
    buf->Map(0, &r, &mapped);
    memcpy(mapped, data, static_cast<size_t>(byteSize));
    buf->Unmap(0, nullptr);
}

void RenderingSystem::BuildMeshBuffers(const std::vector<Vertex>& verts,
                                        const std::vector<UINT>&   idxs)
{
    indexCount_ = static_cast<UINT>(idxs.size());
    UINT64 vbSize = verts.size() * sizeof(Vertex);
    UINT64 ibSize = idxs.size()  * sizeof(UINT);
    MakeUploadBuffer(verts.data(), vbSize, vertexBuffer_);
    MakeUploadBuffer(idxs.data(),  ibSize, indexBuffer_);
    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes    = static_cast<UINT>(vbSize);
    vbView_.StrideInBytes  = sizeof(Vertex);
    ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibView_.SizeInBytes    = static_cast<UINT>(ibSize);
    ibView_.Format         = DXGI_FORMAT_R32_UINT;
}

void RenderingSystem::LoadMeshFromObj(const std::wstring& path)
{
    ObjResult obj = LoadObj(path);
    if (obj.valid) BuildMeshBuffers(obj.vertices, obj.indices);
    else           LoadMeshFromBuiltin();
}

void RenderingSystem::LoadMeshFromBuiltin()
{
    float s = 0.5f;
    Vertex vData[] =
    {
        { {-s,-s, s},{0,0,1},{0.9f,0.2f,0.2f,1},{0,1} },
        { { s,-s, s},{0,0,1},{0.9f,0.2f,0.2f,1},{1,1} },
        { { s, s, s},{0,0,1},{0.9f,0.2f,0.2f,1},{1,0} },
        { {-s, s, s},{0,0,1},{0.9f,0.2f,0.2f,1},{0,0} },
        { { s,-s,-s},{0,0,-1},{0.2f,0.8f,0.2f,1},{0,1} },
        { {-s,-s,-s},{0,0,-1},{0.2f,0.8f,0.2f,1},{1,1} },
        { {-s, s,-s},{0,0,-1},{0.2f,0.8f,0.2f,1},{1,0} },
        { { s, s,-s},{0,0,-1},{0.2f,0.8f,0.2f,1},{0,0} },
        { {-s,-s,-s},{-1,0,0},{0.2f,0.2f,0.9f,1},{0,1} },
        { {-s,-s, s},{-1,0,0},{0.2f,0.2f,0.9f,1},{1,1} },
        { {-s, s, s},{-1,0,0},{0.2f,0.2f,0.9f,1},{1,0} },
        { {-s, s,-s},{-1,0,0},{0.2f,0.2f,0.9f,1},{0,0} },
        { { s,-s, s},{1,0,0},{0.9f,0.9f,0.1f,1},{0,1} },
        { { s,-s,-s},{1,0,0},{0.9f,0.9f,0.1f,1},{1,1} },
        { { s, s,-s},{1,0,0},{0.9f,0.9f,0.1f,1},{1,0} },
        { { s, s, s},{1,0,0},{0.9f,0.9f,0.1f,1},{0,0} },
        { {-s, s, s},{0,1,0},{0.1f,0.9f,0.9f,1},{0,1} },
        { { s, s, s},{0,1,0},{0.1f,0.9f,0.9f,1},{1,1} },
        { { s, s,-s},{0,1,0},{0.1f,0.9f,0.9f,1},{1,0} },
        { {-s, s,-s},{0,1,0},{0.1f,0.9f,0.9f,1},{0,0} },
        { {-s,-s,-s},{0,-1,0},{0.9f,0.1f,0.9f,1},{0,1} },
        { { s,-s,-s},{0,-1,0},{0.9f,0.1f,0.9f,1},{1,1} },
        { { s,-s, s},{0,-1,0},{0.9f,0.1f,0.9f,1},{1,0} },
        { {-s,-s, s},{0,-1,0},{0.9f,0.1f,0.9f,1},{0,0} },
    };
    std::vector<UINT> idxs;
    for (UINT f = 0; f < 6; ++f)
    {
        UINT b = f * 4;
        idxs.push_back(b+0); idxs.push_back(b+1); idxs.push_back(b+2);
        idxs.push_back(b+0); idxs.push_back(b+2); idxs.push_back(b+3);
    }
    std::vector<Vertex> verts(vData, vData + _countof(vData));
    BuildMeshBuffers(verts, idxs);
}

void RenderingSystem::UploadTextureInternal(const TextureData& td, int slot)
{
    if (!td.valid || td.width == 0 || td.height == 0)
        ThrowIfFailed(E_INVALIDARG, "UploadTextureInternal: invalid TextureData");

    ComPtr<ID3D12Resource>& texRes    = textures_[slot];
    ComPtr<ID3D12Resource>& uploadRes = textureUploads_[slot];

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = td.width;
    texDesc.Height           = td.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc       = { 1, 0 };

    ThrowIfFailed(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texRes)));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0; UINT64 rowSize = 0, uploadSize = 0;
    device_->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = uploadSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = { 1, 0 };
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ThrowIfFailed(device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadRes)));

    uint8_t* mapped = nullptr;
    uploadRes->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    UINT srcPitch = td.width * 4;
    for (UINT row = 0; row < td.height; ++row)
        memcpy(mapped + (UINT64)footprint.Footprint.RowPitch * row,
               td.pixels.data() + (UINT64)srcPitch * row, srcPitch);
    uploadRes->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadRes.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texRes.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    commandList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = texRes.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += slot * srvDescSize_;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = 1;
    device_->CreateShaderResourceView(texRes.Get(), &srvDesc, srvHandle);
}

void RenderingSystem::LoadTexture(const TextureData& td, int slot)
{
    ThrowIfFailed(commandAllocators_[0]->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocators_[0].Get(), nullptr));
    UploadTextureInternal(td, slot);
    ThrowIfFailed(commandList_->Close());
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    WaitForGPU();
    textureUploads_[slot].Reset();
}

void RenderingSystem::BeginFrame()
{
    ThrowIfFailed(commandAllocators_[frameIndex_]->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocators_[frameIndex_].Get(), nullptr));
    D3D12_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_); vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor = { 0, 0, width_, height_ };
    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
}

void RenderingSystem::BeginGeometryPass()
{
    gbuffer_.TransitionToRenderTarget(commandList_.Get());

    geomDrawIdx_ = 0; // reset per-draw slot counter

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GBuffer::RT_COUNT];
    for (UINT i = 0; i < GBuffer::RT_COUNT; ++i)
        rtvs[i] = gbuffer_.GetRTV(i);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = gbuffer_.GetDSV();

    gbuffer_.Clear(commandList_.Get());
    commandList_->OMSetRenderTargets(GBuffer::RT_COUNT, rtvs, FALSE, &dsv);
    commandList_->SetPipelineState(geomPSO_.Get());

    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(geomRootSig_.Get());
    commandList_->SetGraphicsRootDescriptorTable(1, srvHeap_->GetGPUDescriptorHandleForHeapStart());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void RenderingSystem::BindGeomStateAndDraw(const GeometryCBData& cb,
                                            D3D12_VERTEX_BUFFER_VIEW vbv,
                                            D3D12_INDEX_BUFFER_VIEW  ibv,
                                            UINT                     idxCount)
{
    // Each draw call gets its own 256-byte-aligned slot so GPU reads
    // the correct CB value even though all writes happen before submission.
    assert(geomDrawIdx_ < MAX_GEOM_DRAWS && "Too many geometry draw calls per frame");
    UINT64 slotOffset = (UINT64)geomDrawIdx_ * sizeof(GeometryCBData);
    memcpy(reinterpret_cast<char*>(geomCBMapped_) + slotOffset, &cb, sizeof(cb));
    commandList_->SetGraphicsRootConstantBufferView(
        0, geomCB_->GetGPUVirtualAddress() + slotOffset);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->IASetIndexBuffer(&ibv);
    commandList_->DrawIndexedInstanced(idxCount, 1, 0, 0, 0);
    ++geomDrawIdx_;
}

void RenderingSystem::DrawSceneMesh(const GeometryCBData& cb)
{
    BindGeomStateAndDraw(cb, vbView_, ibView_, indexCount_);
}

void RenderingSystem::DrawSphere(const GeometryCBData& cb)
{
    BindGeomStateAndDraw(cb, sphereVBView_, sphereIBView_, sphereIndexCount_);
}

void RenderingSystem::EndGeometryPass()
{
    gbuffer_.TransitionToShaderResource(commandList_.Get());
}

void RenderingSystem::DoLightingPass(const LightingCBData& cb)
{
    memcpy(lightCBMapped_, &cb, sizeof(cb));

    D3D12_RESOURCE_BARRIER barrierIn = {};
    barrierIn.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierIn.Transition.pResource   = renderTargets_[frameIndex_].Get();
    barrierIn.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrierIn.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrierIn.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrierIn);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frameIndex_ * rtvDescSize_;
    const float clearColor[] = { 0.02f, 0.02f, 0.05f, 1.0f };
    commandList_->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    commandList_->SetPipelineState(lightPSO_.Get());
    ID3D12DescriptorHeap* heaps[] = { gbuffer_.GetSRVHeap() };
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(lightRootSig_.Get());
    commandList_->SetGraphicsRootConstantBufferView(0, lightCB_->GetGPUVirtualAddress());
    commandList_->SetGraphicsRootDescriptorTable(1, gbuffer_.GetSRVTable());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::EndFrame()
{
    D3D12_RESOURCE_BARRIER barrierOut = {};
    barrierOut.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierOut.Transition.pResource   = renderTargets_[frameIndex_].Get();
    barrierOut.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrierOut.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    barrierOut.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrierOut);
    ThrowIfFailed(commandList_->Close());
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    ThrowIfFailed(swapChain_->Present(1, 0));
    MoveToNextFrame();
}

void RenderingSystem::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (!swapChain_ || !device_)   return;
    WaitForGPU();
    width_  = width;
    height_ = height;
    for (UINT i = 0; i < FRAME_COUNT; ++i) renderTargets_[i].Reset();
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    swapChain_->GetDesc1(&scDesc);
    ThrowIfFailed(swapChain_->ResizeBuffers(FRAME_COUNT,
        static_cast<UINT>(width_), static_cast<UINT>(height_), scDesc.Format, 0));
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    CreateRenderTargetViews();
    gbuffer_.Resize(device_.Get(), static_cast<UINT>(width_), static_cast<UINT>(height_));
}

void RenderingSystem::WaitForGPU()
{
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValues_[frameIndex_]));
    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_));
    ::WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
    ++fenceValues_[frameIndex_];
}

void RenderingSystem::MoveToNextFrame()
{
    const UINT64 current = fenceValues_[frameIndex_];
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), current));
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < fenceValues_[frameIndex_])
    {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_));
        ::WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
    }
    fenceValues_[frameIndex_] = current + 1;
}

ComPtr<ID3DBlob> RenderingSystem::CompileShader(const std::wstring& filename,
                                                  const std::string& entry,
                                                  const std::string& target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    if (GetFileAttributesW(filename.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string narrow(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &narrow[0], len, nullptr, nullptr);
        MessageBoxA(hwnd_, ("Shader not found:\n" + narrow).c_str(), "Error", MB_OK | MB_ICONERROR);
        ThrowIfFailed(E_FAIL, "DeferredShaders.hlsl not found");
    }
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, nullptr,
        entry.c_str(), target.c_str(), flags, 0, &blob, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(hwnd_,
            static_cast<char*>(errors->GetBufferPointer()), "Shader Error", MB_OK | MB_ICONERROR);
        ThrowIfFailed(hr, "CompileShader failed");
    }
    return blob;
}
