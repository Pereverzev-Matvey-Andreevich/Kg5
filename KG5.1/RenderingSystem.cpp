#include "RenderingSystem.h"
#include "Utils.h"
#include <cassert>
#include <cmath>

RenderingSystem::RenderingSystem(HWND hwnd, int w, int h) : hwnd_(hwnd), width_(w), height_(h)
{ for (UINT i=0;i<FRAME_COUNT;++i) fenceValues_[i]=0; }

RenderingSystem::~RenderingSystem()
{
    if (commandQueue_&&fence_&&fenceEvent_) try{WaitForGPU();}catch(...){}
    if (geomCB_&&geomCBMapped_) geomCB_->Unmap(0,nullptr);
    if (lightCB_&&lightCBMapped_) lightCB_->Unmap(0,nullptr);
    if (shadowCB_&&shadowCBMapped_) shadowCB_->Unmap(0,nullptr);
    if (particleCB_&&particleCBMapped_) particleCB_->Unmap(0,nullptr);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

bool RenderingSystem::Initialize()
{
    try {
        CreateDevice(); CreateCommandQueue(); CreateSwapChain();
        CreateDescriptorHeaps(); CreateRenderTargetViews();
        CreateCommandObjects(); CreateFence();
        gbuffer_.Create(device_.Get(),(UINT)width_,(UINT)height_);
        CreateGeometryPassPipeline(); CreateTessellationPSO();
        CreateLightingPassPipeline(); CreateConstantBuffers();
        CreateSphereBuffers();
        CreateShadowResources(); CreateShadowPipeline();
        CreateLightingSRVHeap();
        CreateParticlePipeline(); CreateParticleResources();
    } catch (const std::exception& e) {
        MessageBoxA(hwnd_,e.what(),"RenderingSystem Init Error",MB_OK|MB_ICONERROR);
        return false;
    }
    return true;
}

void RenderingSystem::CreateDevice()
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i=0; factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i)
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)))) return;
    ThrowIfFailed(E_FAIL,"No D3D12 device");
}

void RenderingSystem::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC d={}; d.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device_->CreateCommandQueue(&d,IID_PPV_ARGS(&commandQueue_)));
}

void RenderingSystem::CreateSwapChain()
{
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    DXGI_SWAP_CHAIN_DESC1 d={};
    d.BufferCount=FRAME_COUNT; d.Width=(UINT)width_; d.Height=(UINT)height_;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; d.SampleDesc={1,0};
    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(commandQueue_.Get(),hwnd_,&d,nullptr,nullptr,&sc1));
    factory->MakeWindowAssociation(hwnd_,DXGI_MWA_NO_ALT_ENTER);
    ThrowIfFailed(sc1.As(&swapChain_));
    frameIndex_=swapChain_->GetCurrentBackBufferIndex();
}

void RenderingSystem::CreateDescriptorHeaps()
{
    { D3D12_DESCRIPTOR_HEAP_DESC d={}; d.NumDescriptors=FRAME_COUNT; d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      ThrowIfFailed(device_->CreateDescriptorHeap(&d,IID_PPV_ARGS(&rtvHeap_)));
      rtvDescSize_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); }
    { D3D12_DESCRIPTOR_HEAP_DESC d={}; d.NumDescriptors=4; d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ThrowIfFailed(device_->CreateDescriptorHeap(&d,IID_PPV_ARGS(&srvHeap_)));
      srvDescSize_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); }
}

void RenderingSystem::CreateRenderTargetViews()
{
    auto h=rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i=0;i<FRAME_COUNT;++i) {
        ThrowIfFailed(swapChain_->GetBuffer(i,IID_PPV_ARGS(&renderTargets_[i])));
        device_->CreateRenderTargetView(renderTargets_[i].Get(),nullptr,h); h.ptr+=rtvDescSize_;
    }
}

void RenderingSystem::CreateCommandObjects()
{
    for (UINT i=0;i<FRAME_COUNT;++i)
        ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&commandAllocators_[i])));
    ThrowIfFailed(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,commandAllocators_[0].Get(),nullptr,IID_PPV_ARGS(&commandList_)));
    commandList_->Close();
}

void RenderingSystem::CreateFence()
{
    ThrowIfFailed(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)));
    fenceValues_[0]=fenceValues_[1]=1;
    fenceEvent_=CreateEvent(nullptr,FALSE,FALSE,nullptr);
    if (!fenceEvent_) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
}

void RenderingSystem::CreateGeometryPassPipeline()
{
    D3D12_ROOT_PARAMETER params[2]={};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister=0; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE srvR={}; srvR.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srvR.NumDescriptors=4; srvR.BaseShaderRegister=0;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges=1; params[1].DescriptorTable.pDescriptorRanges=&srvR; params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sam={}; sam.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR; sam.AddressU=sam.AddressV=sam.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP; sam.MaxLOD=D3D12_FLOAT32_MAX; sam.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsD={}; rsD.NumParameters=2; rsD.pParameters=params; rsD.NumStaticSamplers=1; rsD.pStaticSamplers=&sam; rsD.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig,err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsD,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err));
    ThrowIfFailed(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&geomRootSig_)));

    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring sp(ep); sp=sp.substr(0,sp.find_last_of(L"\\/")+1)+L"DeferredShaders.hlsl";
    auto vs=CompileShader(sp,"GeometryVS","vs_5_0"); auto ps=CompileShader(sp,"GeometryPS","ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    };
    D3D12_RASTERIZER_DESC rast={}; rast.FillMode=D3D12_FILL_MODE_SOLID; rast.CullMode=D3D12_CULL_MODE_BACK; rast.DepthClipEnable=TRUE;
    D3D12_DEPTH_STENCIL_DESC ds={}; ds.DepthEnable=TRUE; ds.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL; ds.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    D3D12_BLEND_DESC blend={}; for(UINT i=0;i<GBuffer::RT_COUNT;++i) blend.RenderTarget[i].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC p={}; p.InputLayout={layout,_countof(layout)}; p.pRootSignature=geomRootSig_.Get();
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    p.RasterizerState=rast; p.BlendState=blend; p.DepthStencilState=ds; p.SampleMask=UINT_MAX;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; p.NumRenderTargets=GBuffer::RT_COUNT;
    p.RTVFormats[0]=GBuffer::AlbedoFmt; p.RTVFormats[1]=GBuffer::NormalFmt; p.RTVFormats[2]=GBuffer::WorldPosFmt;
    p.DSVFormat=GBuffer::DepthFmt; p.SampleDesc={1,0};
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&geomPSO_)));
}

void RenderingSystem::CreateTessellationPSO()
{
    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring sp(ep); sp=sp.substr(0,sp.find_last_of(L"\\/")+1)+L"DeferredShaders.hlsl";
    auto vs=CompileShader(sp,"TessVS","vs_5_0"); auto hs=CompileShader(sp,"TessHS","hs_5_0");
    auto ds=CompileShader(sp,"TessDS","ds_5_0"); auto ps=CompileShader(sp,"GeometryPS","ps_5_0");
    D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    };
    D3D12_DEPTH_STENCIL_DESC dss={}; dss.DepthEnable=TRUE; dss.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL; dss.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    D3D12_BLEND_DESC blend={}; for(UINT i=0;i<GBuffer::RT_COUNT;++i) blend.RenderTarget[i].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p={}; p.InputLayout={layout,_countof(layout)}; p.pRootSignature=geomRootSig_.Get();
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.HS={hs->GetBufferPointer(),hs->GetBufferSize()};
    p.DS={ds->GetBufferPointer(),ds->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    p.BlendState=blend; p.DepthStencilState=dss; p.SampleMask=UINT_MAX;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH; p.NumRenderTargets=GBuffer::RT_COUNT;
    p.RTVFormats[0]=GBuffer::AlbedoFmt; p.RTVFormats[1]=GBuffer::NormalFmt; p.RTVFormats[2]=GBuffer::WorldPosFmt;
    p.DSVFormat=GBuffer::DepthFmt; p.SampleDesc={1,0};
    D3D12_RASTERIZER_DESC rs={}; rs.FillMode=D3D12_FILL_MODE_SOLID; rs.CullMode=D3D12_CULL_MODE_NONE; rs.DepthClipEnable=TRUE;
    p.RasterizerState=rs; ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&tessPSO_)));
    rs.FillMode=D3D12_FILL_MODE_WIREFRAME; p.RasterizerState=rs;
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&tessPSOWire_)));
}

void RenderingSystem::CreateLightingPassPipeline()
{
    D3D12_ROOT_PARAMETER params[2]={};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister=0; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    // SRV table now holds GBuffer + shadow array (4 SRVs)
    D3D12_DESCRIPTOR_RANGE gbR={}; gbR.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; gbR.NumDescriptors=GBuffer::RT_COUNT+1; gbR.BaseShaderRegister=0;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges=1; params[1].DescriptorTable.pDescriptorRanges=&gbR; params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    // Two static samplers: s0 = point clamp for GBuffer, s1 = comparison for shadow PCF
    D3D12_STATIC_SAMPLER_DESC sams[2]={};
    sams[0].Filter=D3D12_FILTER_MIN_MAG_MIP_POINT; sams[0].AddressU=sams[0].AddressV=sams[0].AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sams[0].MaxLOD=D3D12_FLOAT32_MAX; sams[0].ShaderRegister=0; sams[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    sams[1].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sams[1].AddressU=sams[1].AddressV=sams[1].AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sams[1].BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sams[1].MaxLOD=D3D12_FLOAT32_MAX;
    sams[1].ShaderRegister=1;
    sams[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsD={}; rsD.NumParameters=2; rsD.pParameters=params; rsD.NumStaticSamplers=2; rsD.pStaticSamplers=sams;
    ComPtr<ID3DBlob> sig,err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsD,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err));
    ThrowIfFailed(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&lightRootSig_)));
    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring sp(ep); sp=sp.substr(0,sp.find_last_of(L"\\/")+1)+L"DeferredShaders.hlsl";
    auto vs=CompileShader(sp,"LightingVS","vs_5_0"); auto ps=CompileShader(sp,"LightingPS","ps_5_0");
    D3D12_DEPTH_STENCIL_DESC dsOff={}; dsOff.DepthEnable=FALSE;
    D3D12_RASTERIZER_DESC rast={}; rast.FillMode=D3D12_FILL_MODE_SOLID; rast.CullMode=D3D12_CULL_MODE_NONE;
    D3D12_BLEND_DESC blend={}; blend.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p={}; p.pRootSignature=lightRootSig_.Get();
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    p.RasterizerState=rast; p.BlendState=blend; p.DepthStencilState=dsOff; p.SampleMask=UINT_MAX;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; p.NumRenderTargets=1;
    p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; p.SampleDesc={1,0};
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&lightPSO_)));
}

void RenderingSystem::CreateConstantBuffers()
{
    auto makeCB=[&](UINT64 sz, ComPtr<ID3D12Resource>& res, void** mapped) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width=sz; rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1; rd.SampleDesc={1,0}; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&res)));
        D3D12_RANGE r={0,0}; ThrowIfFailed(res->Map(0,&r,mapped));
    };
    makeCB(sizeof(GeometryCBData)*MAX_GEOM_DRAWS, geomCB_, (void**)&geomCBMapped_);
    makeCB(sizeof(LightingCBData), lightCB_, (void**)&lightCBMapped_);
    makeCB(sizeof(ShadowCBData)*MAX_SHADOW_DRAWS, shadowCB_, (void**)&shadowCBMapped_);
    makeCB(sizeof(ParticleCBData), particleCB_, (void**)&particleCBMapped_);
}

void RenderingSystem::CreateSphereBuffers()
{
    const int R=12,S=12; const float rad=1.0f,pi=3.14159265f;
    std::vector<Vertex> v; std::vector<UINT> idx;
    for(int r=0;r<=R;++r) { float phi=pi*r/R; for(int s=0;s<=S;++s) { float th=2*pi*s/S;
        float x=sinf(phi)*cosf(th),y=cosf(phi),z=sinf(phi)*sinf(th);
        Vertex vt; vt.Position={x*rad,y*rad,z*rad}; vt.Normal={x,y,z}; vt.Color={0.8f,0,1,0}; vt.TexCoord={(float)s/S,(float)r/R}; v.push_back(vt);
    }}
    for(int r=0;r<R;++r) for(int s=0;s<S;++s) { UINT a=r*(S+1)+s,b=a+(S+1);
        idx.push_back(a);idx.push_back(b);idx.push_back(a+1);idx.push_back(a+1);idx.push_back(b);idx.push_back(b+1);}
    sphereIndexCount_=(UINT)idx.size();
    UINT64 vSz=v.size()*sizeof(Vertex),iSz=idx.size()*sizeof(UINT);
    MakeUploadBuffer(v.data(),vSz,sphereVB_); MakeUploadBuffer(idx.data(),iSz,sphereIB_);
    sphereVBView_={sphereVB_->GetGPUVirtualAddress(),(UINT)vSz,sizeof(Vertex)};
    sphereIBView_={sphereIB_->GetGPUVirtualAddress(),(UINT)iSz,DXGI_FORMAT_R32_UINT};
}

// ===========================================================================
// PARTICLES
// ===========================================================================

void RenderingSystem::CreateParticlePipeline()
{
    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring dir(ep); dir=dir.substr(0,dir.find_last_of(L"\\/")+1);
    std::wstring pp=dir+L"ParticleShaders.hlsl";

    // Compute root sig: CBV(b0) + UAV table(u0,u1,u2)
    {
        D3D12_ROOT_PARAMETER params[2]={};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister=0; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE uR={}; uR.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uR.NumDescriptors=3; uR.BaseShaderRegister=0;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges=1; params[1].DescriptorTable.pDescriptorRanges=&uR; params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsD={}; rsD.NumParameters=2; rsD.pParameters=params;
        ComPtr<ID3DBlob> sig,err;
        ThrowIfFailed(D3D12SerializeRootSignature(&rsD,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err));
        ThrowIfFailed(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&particleComputeRootSig_)));
    }
    { auto cs=CompileShader(pp,"ParticleUpdateCS","cs_5_0");
      D3D12_COMPUTE_PIPELINE_STATE_DESC d={}; d.pRootSignature=particleComputeRootSig_.Get(); d.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
      ThrowIfFailed(device_->CreateComputePipelineState(&d,IID_PPV_ARGS(&particleUpdatePSO_)));
      auto cs2=CompileShader(pp,"ParticleEmitCS","cs_5_0"); d.CS={cs2->GetBufferPointer(),cs2->GetBufferSize()};
      ThrowIfFailed(device_->CreateComputePipelineState(&d,IID_PPV_ARGS(&particleEmitPSO_))); }

    // Render root sig: CBV(b0) + SRV table(t0)
    {
        D3D12_ROOT_PARAMETER params[2]={};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister=0; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE sR={}; sR.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; sR.NumDescriptors=1; sR.BaseShaderRegister=0;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges=1; params[1].DescriptorTable.pDescriptorRanges=&sR; params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsD={}; rsD.NumParameters=2; rsD.pParameters=params;
        ComPtr<ID3DBlob> sig,err;
        ThrowIfFailed(D3D12SerializeRootSignature(&rsD,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err));
        ThrowIfFailed(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&particleRenderRootSig_)));
    }
    // Render PSO: VS->GS->PS, point topology, writes GBuffer
    {
        auto vs=CompileShader(pp,"ParticleVS","vs_5_0"); auto gs=CompileShader(pp,"ParticleGS","gs_5_0"); auto ps=CompileShader(pp,"ParticlePS","ps_5_0");
        D3D12_RASTERIZER_DESC rast={}; rast.FillMode=D3D12_FILL_MODE_SOLID; rast.CullMode=D3D12_CULL_MODE_NONE; rast.DepthClipEnable=TRUE;
        D3D12_DEPTH_STENCIL_DESC dss={}; dss.DepthEnable=TRUE; dss.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL; dss.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        D3D12_BLEND_DESC blend={}; for(UINT i=0;i<GBuffer::RT_COUNT;++i) blend.RenderTarget[i].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p={}; p.InputLayout={nullptr,0}; p.pRootSignature=particleRenderRootSig_.Get();
        p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.GS={gs->GetBufferPointer(),gs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.RasterizerState=rast; p.BlendState=blend; p.DepthStencilState=dss; p.SampleMask=UINT_MAX;
        p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT; p.NumRenderTargets=GBuffer::RT_COUNT;
        p.RTVFormats[0]=GBuffer::AlbedoFmt; p.RTVFormats[1]=GBuffer::NormalFmt; p.RTVFormats[2]=GBuffer::WorldPosFmt;
        p.DSVFormat=GBuffer::DepthFmt; p.SampleDesc={1,0};
        ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&particleRenderPSO_)));
    }
}

void RenderingSystem::CreateParticleResources()
{
    particleDescSize_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT64 poolSz=MAX_GPU_PARTICLES*sizeof(GPUParticle);

    auto makeBuf=[&](UINT64 sz, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& res){
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width=sz; rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1; rd.SampleDesc={1,0}; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags=flags;
        ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&res)));
    };

    makeBuf(poolSz, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, particlePoolA_);
    makeBuf(poolSz, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, particlePoolB_);
    // Two counters — one per pool (D3D12 Append/Consume requires per-UAV counter)
    makeBuf(4,      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, particleCounter_);
    makeBuf(4,      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, particleCounterB_);

    // Upload buffer to reset counter to 0
    { D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=4; rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1; rd.SampleDesc={1,0}; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&particleCounterUpload_)));
      uint32_t* m=nullptr; D3D12_RANGE r={0,0}; particleCounterUpload_->Map(0,&r,(void**)&m); *m=0; particleCounterUpload_->Unmap(0,nullptr);
    }

    // UAV heap: 6 descriptors [ping: A,B,ctr] [pong: B,A,ctr]
    { D3D12_DESCRIPTOR_HEAP_DESC d={}; d.NumDescriptors=6; d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ThrowIfFailed(device_->CreateDescriptorHeap(&d,IID_PPV_ARGS(&particleUAVHeap_))); }

    auto cpuUAV=[&](UINT i)->D3D12_CPU_DESCRIPTOR_HANDLE{ auto h=particleUAVHeap_->GetCPUDescriptorHandleForHeapStart(); h.ptr+=i*particleDescSize_; return h; };

    D3D12_UNORDERED_ACCESS_VIEW_DESC bufUAV={}; bufUAV.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; bufUAV.Buffer.NumElements=MAX_GPU_PARTICLES; bufUAV.Buffer.StructureByteStride=sizeof(GPUParticle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC ctrUAV={}; ctrUAV.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; ctrUAV.Format=DXGI_FORMAT_R32_TYPELESS; ctrUAV.Buffer.NumElements=1; ctrUAV.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;

    // Each pool UAV gets its own counter via pCounterResource.
    // This is what enables AppendStructuredBuffer.Append() and
    // ConsumeStructuredBuffer.Consume() in HLSL — Append writes to the slot
    // pointed at by the counter and increments it, Consume decrements first.
    // Ping [0]=A (Consume), [1]=B (Append), [2]=counter A (also bound for compat)
    device_->CreateUnorderedAccessView(particlePoolA_.Get(), particleCounter_.Get(),  &bufUAV, cpuUAV(0));
    device_->CreateUnorderedAccessView(particlePoolB_.Get(), particleCounterB_.Get(), &bufUAV, cpuUAV(1));
    device_->CreateUnorderedAccessView(particleCounter_.Get(), nullptr, &ctrUAV, cpuUAV(2));
    // Pong [3]=B (Consume), [4]=A (Append), [5]=counter B
    device_->CreateUnorderedAccessView(particlePoolB_.Get(), particleCounterB_.Get(), &bufUAV, cpuUAV(3));
    device_->CreateUnorderedAccessView(particlePoolA_.Get(), particleCounter_.Get(),  &bufUAV, cpuUAV(4));
    device_->CreateUnorderedAccessView(particleCounterB_.Get(), nullptr, &ctrUAV, cpuUAV(5));

    // SRV heap: [0]=poolA SRV, [1]=poolB SRV
    { D3D12_DESCRIPTOR_HEAP_DESC d={}; d.NumDescriptors=2; d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ThrowIfFailed(device_->CreateDescriptorHeap(&d,IID_PPV_ARGS(&particleSRVHeap_))); }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvD={}; srvD.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; srvD.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvD.Buffer.NumElements=MAX_GPU_PARTICLES; srvD.Buffer.StructureByteStride=sizeof(GPUParticle);
    auto cpuSRV=[&](UINT i)->D3D12_CPU_DESCRIPTOR_HANDLE{ auto h=particleSRVHeap_->GetCPUDescriptorHandleForHeapStart(); h.ptr+=i*particleDescSize_; return h; };
    device_->CreateShaderResourceView(particlePoolA_.Get(),&srvD,cpuSRV(0));
    device_->CreateShaderResourceView(particlePoolB_.Get(),&srvD,cpuSRV(1));

    // Zero-init pools via a temp upload
    {
        std::vector<uint8_t> zeros((size_t)poolSz, 0);
        ComPtr<ID3D12Resource> upA, upB;
        MakeUploadBuffer(zeros.data(), poolSz, upA);
        MakeUploadBuffer(zeros.data(), poolSz, upB);
        ThrowIfFailed(commandAllocators_[0]->Reset());
        ThrowIfFailed(commandList_->Reset(commandAllocators_[0].Get(),nullptr));

        D3D12_RESOURCE_BARRIER b[2]={};
        b[0].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b[0].Transition.pResource=particlePoolA_.Get(); b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST; b[0].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b[1]=b[0]; b[1].Transition.pResource=particlePoolB_.Get();
        commandList_->ResourceBarrier(2,b);
        commandList_->CopyBufferRegion(particlePoolA_.Get(),0,upA.Get(),0,poolSz);
        commandList_->CopyBufferRegion(particlePoolB_.Get(),0,upB.Get(),0,poolSz);
        b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b[1]=b[0]; b[1].Transition.pResource=particlePoolB_.Get();
        commandList_->ResourceBarrier(2,b);
        ThrowIfFailed(commandList_->Close());
        ID3D12CommandList* lists[]={commandList_.Get()}; commandQueue_->ExecuteCommandLists(1,lists);
        WaitForGPU();
    }
}

void RenderingSystem::DispatchParticles(const ParticleCBData& cb)
{
    memcpy(particleCBMapped_,&cb,sizeof(cb));
    UINT uavOff = particlePingPong_ ? 3 : 0;

    // Determine which counter is INPUT (Consume) vs OUTPUT (Append) this frame.
    // Ping (pingPong=false): Consume from A, Append to B → reset counter B
    // Pong (pingPong=true) : Consume from B, Append to A → reset counter A
    ID3D12Resource* outputCounter = particlePingPong_ ? particleCounter_.Get() : particleCounterB_.Get();

    // Reset OUTPUT counter to 0 (so Append starts from index 0)
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=outputCounter;
      b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList_->ResourceBarrier(1,&b); }
    commandList_->CopyBufferRegion(outputCounter,0,particleCounterUpload_.Get(),0,4);
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=outputCounter;
      b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList_->ResourceBarrier(1,&b); }

    ID3D12DescriptorHeap* heaps[]={particleUAVHeap_.Get()};
    commandList_->SetDescriptorHeaps(1,heaps);

    auto gpuStart=particleUAVHeap_->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE table; table.ptr=gpuStart.ptr+uavOff*particleDescSize_;

    commandList_->SetComputeRootSignature(particleComputeRootSig_.Get());
    commandList_->SetComputeRootConstantBufferView(0,particleCB_->GetGPUVirtualAddress());
    commandList_->SetComputeRootDescriptorTable(1,table);

    // Update — Consume from input pool, Append survivors to output pool
    commandList_->SetPipelineState(particleUpdatePSO_.Get());
    commandList_->Dispatch((MAX_GPU_PARTICLES+255)/256,1,1);

    // UAV barrier
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource=nullptr;
      commandList_->ResourceBarrier(1,&b); }

    // Emit — Append new particles to output pool
    if (cb.EmitCount > 0) {
        commandList_->SetPipelineState(particleEmitPSO_.Get());
        commandList_->Dispatch((cb.EmitCount+63)/64,1,1);
    }

    // UAV barrier after emit
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource=nullptr;
      commandList_->ResourceBarrier(1,&b); }
}

void RenderingSystem::DrawParticles(const ParticleCBData& cb)
{
    memcpy(particleCBMapped_,&cb,sizeof(cb));

    // Output pool is: ping(false)->poolB, pong(true)->poolA
    ID3D12Resource* outputPool = particlePingPong_ ? particlePoolA_.Get() : particlePoolB_.Get();
    UINT srvSlot = particlePingPong_ ? 0 : 1;  // SRV heap: [0]=A, [1]=B

    // Transition output pool: UAV -> NON_PIXEL_SHADER_RESOURCE (for VS/GS to read)
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=outputPool;
      b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList_->ResourceBarrier(1,&b); }

    // Set particle render state
    ID3D12DescriptorHeap* heaps[]={particleSRVHeap_.Get()};
    commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetGraphicsRootSignature(particleRenderRootSig_.Get());
    commandList_->SetGraphicsRootConstantBufferView(0,particleCB_->GetGPUVirtualAddress());
    auto srvGPU=particleSRVHeap_->GetGPUDescriptorHandleForHeapStart();
    srvGPU.ptr+=srvSlot*particleDescSize_;
    commandList_->SetGraphicsRootDescriptorTable(1,srvGPU);
    commandList_->SetPipelineState(particleRenderPSO_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

    // Draw ALL particles — dead ones have size=0, GS discards them
    commandList_->DrawInstanced(MAX_GPU_PARTICLES,1,0,0);

    // Transition back: NON_PIXEL_SHADER_RESOURCE -> UAV
    { D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=outputPool;
      b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList_->ResourceBarrier(1,&b); }

    // Swap pools for next frame
    particlePingPong_=!particlePingPong_;
}

// ===========================================================================
// UNCHANGED METHODS
// ===========================================================================

void RenderingSystem::MakeUploadBuffer(const void* data, UINT64 sz, ComPtr<ID3D12Resource>& buf)
{
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sz; rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1; rd.SampleDesc={1,0}; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buf)));
    void* m=nullptr; D3D12_RANGE r={0,0}; buf->Map(0,&r,&m); memcpy(m,data,(size_t)sz); buf->Unmap(0,nullptr);
}

void RenderingSystem::BuildMeshBuffers(const std::vector<Vertex>& v, const std::vector<UINT>& idx)
{
    indexCount_=(UINT)idx.size(); UINT64 vSz=v.size()*sizeof(Vertex),iSz=idx.size()*sizeof(UINT);
    MakeUploadBuffer(v.data(),vSz,vertexBuffer_); MakeUploadBuffer(idx.data(),iSz,indexBuffer_);
    vbView_={vertexBuffer_->GetGPUVirtualAddress(),(UINT)vSz,sizeof(Vertex)};
    ibView_={indexBuffer_->GetGPUVirtualAddress(),(UINT)iSz,DXGI_FORMAT_R32_UINT};
}

void RenderingSystem::LoadMeshFromObj(const std::wstring& path) { ObjResult o=LoadObj(path); if(o.valid)BuildMeshBuffers(o.vertices,o.indices); else LoadMeshFromBuiltin(); }

void RenderingSystem::LoadMeshFromBuiltin()
{
    float s=0.5f;
    Vertex vd[]={{{-s,-s,s},{0,0,1},{.9f,.2f,.2f,1},{0,1}},{{s,-s,s},{0,0,1},{.9f,.2f,.2f,1},{1,1}},{{s,s,s},{0,0,1},{.9f,.2f,.2f,1},{1,0}},{{-s,s,s},{0,0,1},{.9f,.2f,.2f,1},{0,0}},
    {{s,-s,-s},{0,0,-1},{.2f,.8f,.2f,1},{0,1}},{{-s,-s,-s},{0,0,-1},{.2f,.8f,.2f,1},{1,1}},{{-s,s,-s},{0,0,-1},{.2f,.8f,.2f,1},{1,0}},{{s,s,-s},{0,0,-1},{.2f,.8f,.2f,1},{0,0}},
    {{-s,-s,-s},{-1,0,0},{.2f,.2f,.9f,1},{0,1}},{{-s,-s,s},{-1,0,0},{.2f,.2f,.9f,1},{1,1}},{{-s,s,s},{-1,0,0},{.2f,.2f,.9f,1},{1,0}},{{-s,s,-s},{-1,0,0},{.2f,.2f,.9f,1},{0,0}},
    {{s,-s,s},{1,0,0},{.9f,.9f,.1f,1},{0,1}},{{s,-s,-s},{1,0,0},{.9f,.9f,.1f,1},{1,1}},{{s,s,-s},{1,0,0},{.9f,.9f,.1f,1},{1,0}},{{s,s,s},{1,0,0},{.9f,.9f,.1f,1},{0,0}},
    {{-s,s,s},{0,1,0},{.1f,.9f,.9f,1},{0,1}},{{s,s,s},{0,1,0},{.1f,.9f,.9f,1},{1,1}},{{s,s,-s},{0,1,0},{.1f,.9f,.9f,1},{1,0}},{{-s,s,-s},{0,1,0},{.1f,.9f,.9f,1},{0,0}},
    {{-s,-s,-s},{0,-1,0},{.9f,.1f,.9f,1},{0,1}},{{s,-s,-s},{0,-1,0},{.9f,.1f,.9f,1},{1,1}},{{s,-s,s},{0,-1,0},{.9f,.1f,.9f,1},{1,0}},{{-s,-s,s},{0,-1,0},{.9f,.1f,.9f,1},{0,0}}};
    std::vector<UINT> idx; for(UINT f=0;f<6;++f){UINT b=f*4;idx.push_back(b);idx.push_back(b+1);idx.push_back(b+2);idx.push_back(b);idx.push_back(b+2);idx.push_back(b+3);}
    BuildMeshBuffers(std::vector<Vertex>(vd,vd+_countof(vd)),idx);
}

void RenderingSystem::UploadTextureInternal(const TextureData& td, int slot)
{
    if(!td.valid||td.width==0||td.height==0) ThrowIfFailed(E_INVALIDARG,"invalid tex");
    auto& tR=textures_[slot]; auto& uR=textureUploads_[slot];
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texD={}; texD.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; texD.Width=td.width; texD.Height=td.height; texD.DepthOrArraySize=1; texD.MipLevels=1; texD.Format=DXGI_FORMAT_R8G8B8A8_UNORM; texD.SampleDesc={1,0};
    ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&texD,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&tR)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={}; UINT nr=0; UINT64 rs=0,us=0;
    device_->GetCopyableFootprints(&texD,0,1,0,&fp,&nr,&rs,&us);
    D3D12_HEAP_PROPERTIES up={}; up.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd={}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=us; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc={1,0}; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ThrowIfFailed(device_->CreateCommittedResource(&up,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uR)));
    uint8_t* mapped=nullptr; uR->Map(0,nullptr,(void**)&mapped); UINT sp2=td.width*4;
    for(UINT row=0;row<td.height;++row) memcpy(mapped+(UINT64)fp.Footprint.RowPitch*row,td.pixels.data()+(UINT64)sp2*row,sp2);
    uR->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION src={}; src.pResource=uR.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
    D3D12_TEXTURE_COPY_LOCATION dst={}; dst.pResource=tR.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER barrier={}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource=tR.Get(); barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1,&barrier);
    auto srvH=srvHeap_->GetCPUDescriptorHandleForHeapStart(); srvH.ptr+=slot*srvDescSize_;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvD={}; srvD.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvD.Format=DXGI_FORMAT_R8G8B8A8_UNORM; srvD.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; srvD.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(tR.Get(),&srvD,srvH);
}

void RenderingSystem::LoadTexture(const TextureData& td, int slot)
{
    ThrowIfFailed(commandAllocators_[0]->Reset()); ThrowIfFailed(commandList_->Reset(commandAllocators_[0].Get(),nullptr));
    UploadTextureInternal(td,slot);
    ThrowIfFailed(commandList_->Close()); ID3D12CommandList* l[]={commandList_.Get()}; commandQueue_->ExecuteCommandLists(1,l); WaitForGPU(); textureUploads_[slot].Reset();
}

// ===========================================================================
// SHADOWS (CSM)
// ===========================================================================

void RenderingSystem::CreateShadowResources()
{
    // Texture2DArray, R32_TYPELESS so we can have D32_FLOAT DSV + R32_FLOAT SRV
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={};
    td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=SHADOW_MAP_SIZE; td.Height=SHADOW_MAP_SIZE;
    td.DepthOrArraySize=CASCADE_COUNT;
    td.MipLevels=1;
    td.Format=DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc={1,0};
    td.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE cv={}; cv.Format=DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth=1.0f;
    ThrowIfFailed(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&shadowMap_)));

    // DSV heap with one DSV per cascade
    D3D12_DESCRIPTOR_HEAP_DESC dhd={}; dhd.NumDescriptors=CASCADE_COUNT; dhd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device_->CreateDescriptorHeap(&dhd,IID_PPV_ARGS(&shadowDsvHeap_)));
    shadowDsvDescSize_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    for(UINT i=0;i<CASCADE_COUNT;++i){
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvD={};
        dsvD.Format=DXGI_FORMAT_D32_FLOAT;
        dsvD.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvD.Texture2DArray.FirstArraySlice=i;
        dsvD.Texture2DArray.ArraySize=1;
        dsvD.Texture2DArray.MipSlice=0;
        auto h=shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart(); h.ptr+=i*shadowDsvDescSize_;
        device_->CreateDepthStencilView(shadowMap_.Get(),&dsvD,h);
    }

    // Отдельный хип на 1 дескриптор — для alpha-test текстуры теней
    { D3D12_DESCRIPTOR_HEAP_DESC hd={};
      hd.NumDescriptors=1;
      hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ThrowIfFailed(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&shadowAlphaHeap_))); }

    // Создаём белый 1×1 пиксель как дефолтную alpha-текстуру.
    // Белый = alpha 1.0 = ничего не отбрасывается — тени непрозрачные по умолчанию.
    InitDefaultShadowAlphaTexture();
}

// Вспомогательная функция: записать текстуру в shadowAlphaTexture_ и создать SRV.
// Вызывается как из InitDefault... (белый пиксель), так и из LoadShadowAlphaTexture.
static void UploadShadowAlphaTex(
    ID3D12Device* dev, ID3D12GraphicsCommandList* cmd,
    UINT w, UINT h, const uint8_t* rgba,
    ComPtr<ID3D12Resource>& texOut, ComPtr<ID3D12Resource>& uploadOut,
    ComPtr<ID3D12DescriptorHeap>& heap)
{
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={}; td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=w; td.Height=h; td.DepthOrArraySize=1; td.MipLevels=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc={1,0};

    texOut.Reset();
    ThrowIfFailed(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texOut)));

    // Upload буфер
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={}; UINT64 us=0;
    dev->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&us);

    D3D12_HEAP_PROPERTIES up={}; up.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd={}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width=us; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1;
    bd.SampleDesc={1,0}; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    uploadOut.Reset();
    ThrowIfFailed(dev->CreateCommittedResource(&up,D3D12_HEAP_FLAG_NONE,&bd,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploadOut)));

    uint8_t* mapped=nullptr; uploadOut->Map(0,nullptr,(void**)&mapped);
    for(UINT row=0;row<h;++row)
        memcpy(mapped+(UINT64)fp.Footprint.RowPitch*row, rgba+(UINT64)w*4*row, (UINT64)w*4);
    uploadOut->Unmap(0,nullptr);

    D3D12_TEXTURE_COPY_LOCATION src={}; src.pResource=uploadOut.Get();
    src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
    D3D12_TEXTURE_COPY_LOCATION dst={}; dst.pResource=texOut.Get();
    dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);

    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=texOut.Get();
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1,&b);

    // Создаём SRV в слоте 0 нашего хипа
    D3D12_SHADER_RESOURCE_VIEW_DESC srvD={};
    srvD.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srvD.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srvD.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvD.Texture2D.MipLevels=1;
    dev->CreateShaderResourceView(texOut.Get(),&srvD,
        heap->GetCPUDescriptorHandleForHeapStart());
}

void RenderingSystem::InitDefaultShadowAlphaTexture()
{
    // Белый 1×1 пиксель — alpha=255, никакие пиксели не отбрасываются
    uint8_t white[4]={255,255,255,255};
    ThrowIfFailed(commandAllocators_[0]->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocators_[0].Get(),nullptr));
    UploadShadowAlphaTex(device_.Get(),commandList_.Get(),1,1,white,
        shadowAlphaTexture_,shadowAlphaUpload_,shadowAlphaHeap_);
    ThrowIfFailed(commandList_->Close());
    ID3D12CommandList* l[]={commandList_.Get()}; commandQueue_->ExecuteCommandLists(1,l);
    WaitForGPU();
    shadowAlphaUpload_.Reset(); // upload буфер больше не нужен
}

void RenderingSystem::LoadShadowAlphaTexture(const TextureData& td)
{
    // Проверка
    if(!td.valid||td.width==0||td.height==0)
        ThrowIfFailed(E_INVALIDARG,"LoadShadowAlphaTexture: invalid TextureData");
    // Загружаем пиксели
    ThrowIfFailed(commandAllocators_[0]->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocators_[0].Get(),nullptr));
    UploadShadowAlphaTex(device_.Get(),commandList_.Get(),
        td.width,td.height,td.pixels.data(),
        shadowAlphaTexture_,shadowAlphaUpload_,shadowAlphaHeap_);
    ThrowIfFailed(commandList_->Close());
    ID3D12CommandList* l[]={commandList_.Get()}; commandQueue_->ExecuteCommandLists(1,l);
    WaitForGPU();
    shadowAlphaUpload_.Reset();
}

void RenderingSystem::CreateShadowPipeline()
{
    // Root sig: CBV(b0) + SRV table(t0) for alpha-test texture + static sampler(s0)
    D3D12_DESCRIPTOR_RANGE srvRange={};
    srvRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors=1;
    srvRange.BaseShaderRegister=0;

    D3D12_ROOT_PARAMETER params[2]={};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister=0;
    params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges=1;
    params[1].DescriptorTable.pDescriptorRanges=&srvRange;
    params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler s0 for texture sampling in alpha test PS
    D3D12_STATIC_SAMPLER_DESC sam={};
    sam.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sam.AddressU=sam.AddressV=sam.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sam.MaxLOD=D3D12_FLOAT32_MAX;
    sam.ShaderRegister=0;
    sam.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsD={};
    rsD.NumParameters=2; rsD.pParameters=params;
    rsD.NumStaticSamplers=1; rsD.pStaticSamplers=&sam;
    rsD.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig,err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsD,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err));
    ThrowIfFailed(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&shadowRootSig_)));

    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring sp(ep); sp=sp.substr(0,sp.find_last_of(L"\\/")+1)+L"DeferredShaders.hlsl";
    auto vs=CompileShader(sp,"ShadowVS","vs_5_0");
    auto ps=CompileShader(sp,"ShadowPS","ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC p={};
    p.InputLayout={layout,_countof(layout)};
    p.pRootSignature=shadowRootSig_.Get();
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};  // PS нужен для alpha test

    D3D12_RASTERIZER_DESC rast={};
    rast.FillMode=D3D12_FILL_MODE_SOLID;
    rast.CullMode=D3D12_CULL_MODE_NONE;
    rast.DepthClipEnable=TRUE;
    rast.DepthBias=2000;
    rast.SlopeScaledDepthBias=4.0f;
    rast.DepthBiasClamp=0.0f;
    p.RasterizerState=rast;

    D3D12_DEPTH_STENCIL_DESC dss={};
    dss.DepthEnable=TRUE;
    dss.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    dss.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    p.DepthStencilState=dss;

    D3D12_BLEND_DESC blend={};
    p.BlendState=blend;
    p.SampleMask=UINT_MAX;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets=0;   // depth-only output
    p.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    p.SampleDesc={1,0};
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&shadowPSO_)));
}

void RenderingSystem::CreateLightingSRVHeap()
{
    // Heap holds [0..2] = GBuffer SRVs (copied from gbuffer's heap), [3] = shadow array SRV
    D3D12_DESCRIPTOR_HEAP_DESC d={};
    d.NumDescriptors=GBuffer::RT_COUNT+1;
    d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&d,IID_PPV_ARGS(&lightingSRVHeap_)));

    UINT inc=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Copy gbuffer SRVs into [0..RT_COUNT-1]
    auto destStart=lightingSRVHeap_->GetCPUDescriptorHandleForHeapStart();
    auto srcStart =gbuffer_.GetSRVHeap()->GetCPUDescriptorHandleForHeapStart();
    device_->CopyDescriptorsSimple(GBuffer::RT_COUNT,destStart,srcStart,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create shadow Texture2DArray SRV at slot RT_COUNT
    D3D12_SHADER_RESOURCE_VIEW_DESC srvD={};
    srvD.Format=DXGI_FORMAT_R32_FLOAT;  // typeless underlying, sampled as float
    srvD.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvD.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvD.Texture2DArray.MostDetailedMip=0;
    srvD.Texture2DArray.MipLevels=1;
    srvD.Texture2DArray.FirstArraySlice=0;
    srvD.Texture2DArray.ArraySize=CASCADE_COUNT;
    srvD.Texture2DArray.PlaneSlice=0;

    D3D12_CPU_DESCRIPTOR_HANDLE shadowSlot=destStart;
    shadowSlot.ptr += GBuffer::RT_COUNT*inc;
    device_->CreateShaderResourceView(shadowMap_.Get(),&srvD,shadowSlot);
}

void RenderingSystem::BeginShadowPass(int cascade)
{
    currentShadowCascade_=cascade;
    if(cascade==0){
        // First-frame: shadowMap_ is in DEPTH_WRITE (initial state) — no transition needed.
        // Subsequent frames: it's in PIXEL_SHADER_RESOURCE — transition back to DEPTH_WRITE.
        if(shadowMapInPSR_){
            D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource=shadowMap_.Get();
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList_->ResourceBarrier(1,&b);
        }
        shadowDrawIdx_=0;
    }

    // Bind this cascade's DSV
    auto dsv=shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += cascade*shadowDsvDescSize_;
    commandList_->OMSetRenderTargets(0,nullptr,FALSE,&dsv);
    commandList_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);

    // Viewport covers full slice
    D3D12_VIEWPORT vp={}; vp.Width=(float)SHADOW_MAP_SIZE; vp.Height=(float)SHADOW_MAP_SIZE; vp.MaxDepth=1;
    D3D12_RECT sc={0,0,(LONG)SHADOW_MAP_SIZE,(LONG)SHADOW_MAP_SIZE};
    commandList_->RSSetViewports(1,&vp); commandList_->RSSetScissorRects(1,&sc);

    commandList_->SetGraphicsRootSignature(shadowRootSig_.Get());
    commandList_->SetPipelineState(shadowPSO_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Привязываем ОТДЕЛЬНЫЙ heap с пользовательской alpha-текстурой (или white по умолчанию)
    ID3D12DescriptorHeap* texHeaps[]={shadowAlphaHeap_.Get()};
    commandList_->SetDescriptorHeaps(1,texHeaps);
    commandList_->SetGraphicsRootDescriptorTable(1,shadowAlphaHeap_->GetGPUDescriptorHandleForHeapStart());
}

void RenderingSystem::BindShadowAndDraw(const ShadowCBData& cb, D3D12_VERTEX_BUFFER_VIEW vbv, D3D12_INDEX_BUFFER_VIEW ibv, UINT idxCount)
{
    assert(shadowDrawIdx_<MAX_SHADOW_DRAWS);
    UINT64 off=(UINT64)shadowDrawIdx_*sizeof(ShadowCBData);
    memcpy((char*)shadowCBMapped_+off,&cb,sizeof(cb));
    // Root param 0 = CBV (World + LightVP + alpha test data)
    commandList_->SetGraphicsRootConstantBufferView(0,shadowCB_->GetGPUVirtualAddress()+off);
    // Root param 1 = SRV table already bound once in BeginShadowPass
    commandList_->IASetVertexBuffers(0,1,&vbv); commandList_->IASetIndexBuffer(&ibv);
    commandList_->DrawIndexedInstanced(idxCount,1,0,0,0); ++shadowDrawIdx_;
}

void RenderingSystem::DrawShadowMesh(const ShadowCBData& cb)
{ BindShadowAndDraw(cb,vbView_,ibView_,indexCount_); }

void RenderingSystem::DrawShadowSphere(const ShadowCBData& cb)
{ BindShadowAndDraw(cb,sphereVBView_,sphereIBView_,sphereIndexCount_); }

void RenderingSystem::EndShadowPass()
{
    // Transition entire shadow texture to PIXEL_SHADER_RESOURCE for use in lighting pass
    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=shadowMap_.Get();
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1,&b);
    shadowMapInPSR_=true;

    // Restore main viewport for subsequent passes
    D3D12_VIEWPORT vp={}; vp.Width=(float)width_; vp.Height=(float)height_; vp.MaxDepth=1;
    D3D12_RECT sc={0,0,width_,height_};
    commandList_->RSSetViewports(1,&vp); commandList_->RSSetScissorRects(1,&sc);
}


void RenderingSystem::BeginFrame()
{
    ThrowIfFailed(commandAllocators_[frameIndex_]->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocators_[frameIndex_].Get(),nullptr));
    D3D12_VIEWPORT vp={}; vp.Width=(float)width_; vp.Height=(float)height_; vp.MaxDepth=1;
    D3D12_RECT sc={0,0,width_,height_};
    commandList_->RSSetViewports(1,&vp); commandList_->RSSetScissorRects(1,&sc);
}

void RenderingSystem::BeginGeometryPass()
{
    gbuffer_.TransitionToRenderTarget(commandList_.Get());
    geomDrawIdx_=0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GBuffer::RT_COUNT];
    for(UINT i=0;i<GBuffer::RT_COUNT;++i) rtvs[i]=gbuffer_.GetRTV(i);
    auto dsv=gbuffer_.GetDSV();
    gbuffer_.Clear(commandList_.Get());
    commandList_->OMSetRenderTargets(GBuffer::RT_COUNT,rtvs,FALSE,&dsv);
    ID3D12DescriptorHeap* h[]={srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1,h);
    commandList_->SetGraphicsRootSignature(geomRootSig_.Get());
    commandList_->SetGraphicsRootDescriptorTable(1,srvHeap_->GetGPUDescriptorHandleForHeapStart());
}

void RenderingSystem::BindGeomStateAndDraw(const GeometryCBData& cb, D3D12_VERTEX_BUFFER_VIEW vbv, D3D12_INDEX_BUFFER_VIEW ibv, UINT idxCount)
{
    assert(geomDrawIdx_<MAX_GEOM_DRAWS);
    UINT64 off=(UINT64)geomDrawIdx_*sizeof(GeometryCBData);
    memcpy((char*)geomCBMapped_+off,&cb,sizeof(cb));
    commandList_->SetGraphicsRootConstantBufferView(0,geomCB_->GetGPUVirtualAddress()+off);
    commandList_->IASetVertexBuffers(0,1,&vbv); commandList_->IASetIndexBuffer(&ibv);
    commandList_->DrawIndexedInstanced(idxCount,1,0,0,0); ++geomDrawIdx_;
}

void RenderingSystem::DrawSceneMesh(const GeometryCBData& cb)
{ commandList_->SetPipelineState(geomPSO_.Get()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); BindGeomStateAndDraw(cb,vbView_,ibView_,indexCount_); }

void RenderingSystem::DrawSceneMeshTess(const GeometryCBData& cb)
{ commandList_->SetPipelineState(wireframe_?tessPSOWire_.Get():tessPSO_.Get()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST); BindGeomStateAndDraw(cb,vbView_,ibView_,indexCount_); }

void RenderingSystem::SetWireframe(bool on){wireframe_=on;}

void RenderingSystem::DrawSphere(const GeometryCBData& cb)
{ commandList_->SetPipelineState(geomPSO_.Get()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); BindGeomStateAndDraw(cb,sphereVBView_,sphereIBView_,sphereIndexCount_); }

void RenderingSystem::EndGeometryPass(){ gbuffer_.TransitionToShaderResource(commandList_.Get()); }

void RenderingSystem::DoLightingPass(const LightingCBData& cb)
{
    memcpy(lightCBMapped_,&cb,sizeof(cb));
    D3D12_RESOURCE_BARRIER bIn={}; bIn.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bIn.Transition.pResource=renderTargets_[frameIndex_].Get(); bIn.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT; bIn.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET; bIn.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1,&bIn);
    auto rtv=rtvHeap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr+=frameIndex_*rtvDescSize_;
    const float cc[]={.02f,.02f,.05f,1}; commandList_->ClearRenderTargetView(rtv,cc,0,nullptr);
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    commandList_->SetPipelineState(lightPSO_.Get());
    // Use combined SRV heap: [0..2]=gbuffer, [3]=shadow array
    ID3D12DescriptorHeap* h[]={lightingSRVHeap_.Get()}; commandList_->SetDescriptorHeaps(1,h);
    commandList_->SetGraphicsRootSignature(lightRootSig_.Get());
    commandList_->SetGraphicsRootConstantBufferView(0,lightCB_->GetGPUVirtualAddress());
    commandList_->SetGraphicsRootDescriptorTable(1,lightingSRVHeap_->GetGPUDescriptorHandleForHeapStart());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3,1,0,0);
}

void RenderingSystem::EndFrame()
{
    D3D12_RESOURCE_BARRIER bOut={}; bOut.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bOut.Transition.pResource=renderTargets_[frameIndex_].Get(); bOut.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; bOut.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT; bOut.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1,&bOut);
    ThrowIfFailed(commandList_->Close()); ID3D12CommandList* l[]={commandList_.Get()}; commandQueue_->ExecuteCommandLists(1,l);
    ThrowIfFailed(swapChain_->Present(1,0)); MoveToNextFrame();
}

void RenderingSystem::Resize(int w, int h)
{
    if(w<=0||h<=0||!swapChain_||!device_) return;
    WaitForGPU(); width_=w; height_=h;
    for(UINT i=0;i<FRAME_COUNT;++i) renderTargets_[i].Reset();
    DXGI_SWAP_CHAIN_DESC1 d={}; swapChain_->GetDesc1(&d);
    ThrowIfFailed(swapChain_->ResizeBuffers(FRAME_COUNT,(UINT)w,(UINT)h,d.Format,0));
    frameIndex_=swapChain_->GetCurrentBackBufferIndex();
    CreateRenderTargetViews(); gbuffer_.Resize(device_.Get(),(UINT)w,(UINT)h);
    // gbuffer SRVs were recreated → repopulate combined lighting heap
    if(lightingSRVHeap_){
        UINT inc=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto destStart=lightingSRVHeap_->GetCPUDescriptorHandleForHeapStart();
        auto srcStart =gbuffer_.GetSRVHeap()->GetCPUDescriptorHandleForHeapStart();
        device_->CopyDescriptorsSimple(GBuffer::RT_COUNT,destStart,srcStart,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void RenderingSystem::WaitForGPU()
{
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(),fenceValues_[frameIndex_]));
    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValues_[frameIndex_],fenceEvent_));
    WaitForSingleObjectEx(fenceEvent_,INFINITE,FALSE); ++fenceValues_[frameIndex_];
}

void RenderingSystem::MoveToNextFrame()
{
    const UINT64 cur=fenceValues_[frameIndex_];
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(),cur));
    frameIndex_=swapChain_->GetCurrentBackBufferIndex();
    if(fence_->GetCompletedValue()<fenceValues_[frameIndex_])
    { ThrowIfFailed(fence_->SetEventOnCompletion(fenceValues_[frameIndex_],fenceEvent_)); WaitForSingleObjectEx(fenceEvent_,INFINITE,FALSE); }
    fenceValues_[frameIndex_]=cur+1;
}

ComPtr<ID3DBlob> RenderingSystem::CompileShader(const std::wstring& fn, const std::string& entry, const std::string& target)
{
    UINT flags=D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags|=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags|=D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    if(GetFileAttributesW(fn.c_str())==INVALID_FILE_ATTRIBUTES){
        int len=WideCharToMultiByte(CP_UTF8,0,fn.c_str(),-1,nullptr,0,nullptr,nullptr);
        std::string narrow(len,0); WideCharToMultiByte(CP_UTF8,0,fn.c_str(),-1,&narrow[0],len,nullptr,nullptr);
        MessageBoxA(hwnd_,("Shader not found:\n"+narrow).c_str(),"Error",MB_OK|MB_ICONERROR);
        ThrowIfFailed(E_FAIL,"Shader not found");
    }
    ComPtr<ID3DBlob> blob,errors;
    HRESULT hr=D3DCompileFromFile(fn.c_str(),nullptr,nullptr,entry.c_str(),target.c_str(),flags,0,&blob,&errors);
    if(FAILED(hr)){ if(errors) MessageBoxA(hwnd_,(char*)errors->GetBufferPointer(),"Shader Error",MB_OK|MB_ICONERROR); ThrowIfFailed(hr,"CompileShader"); }
    return blob;
}
