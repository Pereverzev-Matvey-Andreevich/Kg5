#pragma once
#include <DirectXMath.h>
using namespace DirectX;

struct Vertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT4 Color;
    XMFLOAT2 TexCoord;
};

struct QuadVertex
{
    XMFLOAT2 Position;
    XMFLOAT2 TexCoord;
};

enum class LightType : int
{
    Point       = 0,
    Directional = 1,
    Spot        = 2
};

struct LightData
{
    XMFLOAT4 Position;
    XMFLOAT4 Direction;
    XMFLOAT4 Color;
    int      Type;
    float    Range;
    float    InnerConeAngle;
    float    OuterConeAngle;
};

#define MAX_LIGHTS 64

struct GeometryCBData
{
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Proj;
    XMFLOAT2 Tiling;
    XMFLOAT2 UVOffset;
    XMFLOAT4 CameraPos;
    float    DisplacementScale;
    float    Pad[7];
};
static_assert(sizeof(GeometryCBData) == 256, "GeometryCBData must be 256 bytes");

struct LightingCBData
{
    XMFLOAT4  CameraPos;
    int       LightCount;
    float     HeaderPad[3];
    LightData Lights[MAX_LIGHTS];
    float     CBPad[56];
};
static_assert(sizeof(LightingCBData) % 256 == 0, "LightingCBData must be multiple of 256 bytes");
