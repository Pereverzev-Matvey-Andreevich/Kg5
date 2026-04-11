#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include "Types.h"
#include "GBuffer.h"
#include "TextureLoader.h"
#include "ObjLoader.h"

using Microsoft::WRL::ComPtr;

static const UINT FRAME_COUNT = 2;

class RenderingSystem
{
public:
    RenderingSystem(HWND hwnd, int width, int height);
    ~RenderingSystem();

    bool Initialize();
    void Resize(int width, int height);

    void BeginFrame();
    void BeginGeometryPass();
    void DrawSceneMesh(const GeometryCBData& cb);
    void DrawSceneMeshTess(const GeometryCBData& cb);
    void DrawSphere(const GeometryCBData& cb);
    void EndGeometryPass();
    void DoLightingPass(const LightingCBData& cb);
    void EndFrame();
    void SetWireframe(bool on);

    void LoadMeshFromObj(const std::wstring& path);
    void LoadMeshFromBuiltin();
    void LoadTexture(const TextureData& td, int slot);

    ID3D12Device* GetDevice()      const { return device_.Get(); }
    UINT          GetFrameIndex()  const { return frameIndex_; }
    int           GetWidth()       const { return width_; }
    int           GetHeight()      const { return height_; }

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain();
    void CreateDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateCommandObjects();
    void CreateFence();
    void CreateGeometryPassPipeline();
    void CreateTessellationPSO();
    void CreateLightingPassPipeline();
    void CreateConstantBuffers();
    void CreateSphereBuffers();

    void WaitForGPU();
    void MoveToNextFrame();

    void BuildMeshBuffers(const std::vector<Vertex>& verts,
                          const std::vector<UINT>&   idxs);
    void MakeUploadBuffer(const void* data, UINT64 byteSize,
                          ComPtr<ID3D12Resource>& buf);
    void UploadTextureInternal(const TextureData& td, int slot);

    ComPtr<ID3DBlob> CompileShader(const std::wstring& path,
                                   const std::string& entry,
                                   const std::string& target);

    void BindGeomStateAndDraw(const GeometryCBData& cb,
                              D3D12_VERTEX_BUFFER_VIEW vbv,
                              D3D12_INDEX_BUFFER_VIEW  ibv,
                              UINT                     idxCount);

    HWND hwnd_;
    int  width_;
    int  height_;

    ComPtr<ID3D12Device>              device_;
    ComPtr<ID3D12CommandQueue>        commandQueue_;
    ComPtr<IDXGISwapChain3>           swapChain_;

    ComPtr<ID3D12DescriptorHeap>      rtvHeap_;
    ComPtr<ID3D12DescriptorHeap>      srvHeap_;
    UINT                              rtvDescSize_ = 0;
    UINT                              srvDescSize_ = 0;

    ComPtr<ID3D12Resource>            renderTargets_[FRAME_COUNT];

    ComPtr<ID3D12CommandAllocator>    commandAllocators_[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> commandList_;

    ComPtr<ID3D12Fence>               fence_;
    UINT64                            fenceValues_[FRAME_COUNT] = {};
    HANDLE                            fenceEvent_ = nullptr;
    UINT                              frameIndex_ = 0;

    GBuffer                           gbuffer_;

    ComPtr<ID3D12RootSignature>       geomRootSig_;
    ComPtr<ID3D12PipelineState>       geomPSO_;
    ComPtr<ID3D12PipelineState>       tessPSO_;
    ComPtr<ID3D12PipelineState>       tessPSOWire_;
    bool                              wireframe_ = false;

    ComPtr<ID3D12RootSignature>       lightRootSig_;
    ComPtr<ID3D12PipelineState>       lightPSO_;

    ComPtr<ID3D12Resource>            vertexBuffer_;
    ComPtr<ID3D12Resource>            indexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW          vbView_ = {};
    D3D12_INDEX_BUFFER_VIEW           ibView_ = {};
    UINT                              indexCount_ = 0;

    ComPtr<ID3D12Resource>            sphereVB_;
    ComPtr<ID3D12Resource>            sphereIB_;
    D3D12_VERTEX_BUFFER_VIEW          sphereVBView_ = {};
    D3D12_INDEX_BUFFER_VIEW           sphereIBView_ = {};
    UINT                              sphereIndexCount_ = 0;

    ComPtr<ID3D12Resource>            textures_[4];
    ComPtr<ID3D12Resource>            textureUploads_[4];

    static const UINT MAX_GEOM_DRAWS = 4096;

    ComPtr<ID3D12Resource>            geomCB_;
    GeometryCBData*                   geomCBMapped_ = nullptr;
    UINT                              geomDrawIdx_  = 0;

    ComPtr<ID3D12Resource>            lightCB_;
    LightingCBData*                   lightCBMapped_ = nullptr;
};
