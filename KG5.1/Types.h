#pragma once
#include <DirectXMath.h>
using namespace DirectX;

struct Vertex { XMFLOAT3 Position; XMFLOAT3 Normal; XMFLOAT4 Color; XMFLOAT2 TexCoord; };
struct QuadVertex { XMFLOAT2 Position; XMFLOAT2 TexCoord; };
enum class LightType : int { Point=0, Directional=1, Spot=2 };
struct LightData { XMFLOAT4 Position; XMFLOAT4 Direction; XMFLOAT4 Color; int Type; float Range; float InnerConeAngle; float OuterConeAngle; };
#define MAX_LIGHTS 64
enum class TessImportance { High=0, Medium=1, Low=2, None=3 };
inline void TessImportanceToRange(TessImportance imp, float& outMin, float& outMax) {
    switch(imp){ case TessImportance::High:outMin=1;outMax=32;break; case TessImportance::Medium:outMin=1;outMax=16;break; case TessImportance::Low:outMin=1;outMax=4;break; default:outMin=1;outMax=1;break; }
}

struct GeometryCBData { XMMATRIX World; XMMATRIX View; XMMATRIX Proj; XMFLOAT2 Tiling; XMFLOAT2 UVOffset; XMFLOAT4 CameraPos; float DisplacementScale; float TessMin; float TessMax; float Pad[5]; };
static_assert(sizeof(GeometryCBData)==256,"GeometryCBData must be 256 bytes");

// ---------------- Shadows / Cascaded Shadow Maps ----------------
#define CASCADE_COUNT 4
#define SHADOW_MAP_SIZE 1024

// Per-draw CB for shadow pass: World + LightVP + alpha-test params
struct ShadowCBData {
    XMMATRIX World;          // 64
    XMMATRIX LightVP;        // 64
    XMFLOAT2 UVTiling;       // 8   — UV множитель (для тайлинга текстуры)
    float    AlphaCutoff;    // 4   — порог прозрачности (обычно 0.5)
    float    UseAlphaTest;   // 4   — 1=тест включён, 0=объект непрозрачный
    float    Pad[28];        // 112 -> итого 256
};
static_assert(sizeof(ShadowCBData)==256,"ShadowCBData must be 256 bytes");

// Lighting CB now carries cascade matrices + split distances + light direction + shadow params.
struct LightingCBData {
    XMFLOAT4 CameraPos;                    // 16
    int      LightCount;
    int      CascadeCount;
    float    ShadowMapSize;
    float    ShadowBias;                   // 16  -> 32
    LightData Lights[MAX_LIGHTS];          // 4096 -> 4128
    XMMATRIX LightVP[CASCADE_COUNT];       // 256  -> 4384  (matrices for each cascade)
    XMFLOAT4 CascadeSplits;                // 16   -> 4400  (.xyzw = far distance per cascade)
    XMFLOAT4 LightDir;                     // 16   -> 4416  (direction of shadowed dir light)
    float    CBPad[48];                    // 192  -> 4608  (multiple of 256)
};
static_assert(sizeof(LightingCBData)%256==0,"LightingCBData must be multiple of 256 bytes");

struct GPUParticle { XMFLOAT3 Position; float Age; XMFLOAT3 Velocity; float MaxAge; };
static_assert(sizeof(GPUParticle)==32,"GPUParticle must be 32 bytes");

#define MAX_GPU_PARTICLES 8192

struct ParticleCBData {
    float DeltaTime; float Gravity; uint32_t MaxParticles; uint32_t EmitCount;   // 16
    XMFLOAT3 EmitPosition; float LifeTime;                                       // 32
    XMFLOAT3 EmitVelMin; float Time;                                              // 48
    XMFLOAT3 EmitVelMax; float Pad1;                                              // 64
    XMMATRIX View;                                                                // 128
    XMMATRIX Proj;                                                                // 192
    XMFLOAT4 CamPosAndSize;                                                       // 208
    XMFLOAT4 CamRight;                                                            // 224
    XMFLOAT4 CamUp;                                                               // 240
    XMFLOAT4 ParticleColor;                                                        // 256
};
static_assert(sizeof(ParticleCBData)==256,"ParticleCBData must be 256 bytes");
