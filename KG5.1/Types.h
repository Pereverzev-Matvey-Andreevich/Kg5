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

// -----------------------------------------------------------------------
// Пресеты важности объекта: определяют диапазон уровней тесселяции.
//
//   HIGH   — колонны, арки, детализированные элементы Sponza (до 32х)
//   MEDIUM — обычные стены, полы, средний уровень деталей (до 16х)
//   LOW    — задние планы, потолки, малозначимые объекты (до 4х)
//   NONE   — тесселяция 1 (отключена, для снарядов / дебаг-объектов)
// -----------------------------------------------------------------------
enum class TessImportance
{
    High   = 0,
    Medium = 1,
    Low    = 2,
    None   = 3
};

// Вспомогательная функция — преобразует пресет в конкретные значения
inline void TessImportanceToRange(TessImportance imp,
                                   float& outMin, float& outMax)
{
    switch (imp)
    {
    case TessImportance::High:
        outMin = 1.0f;  outMax = 32.0f; break;
    case TessImportance::Medium:
        outMin = 1.0f;  outMax = 16.0f; break;
    case TessImportance::Low:
        outMin = 1.0f;  outMax = 4.0f;  break;
    case TessImportance::None:
    default:
        outMin = 1.0f;  outMax = 1.0f;  break;
    }
}

struct GeometryCBData
{
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Proj;
    XMFLOAT2 Tiling;
    XMFLOAT2 UVOffset;
    XMFLOAT4 CameraPos;
    float    DisplacementScale;
    float    TessMin;          // минимальный фактор тесселяции (вдали)
    float    TessMax;          // максимальный фактор тесселяции (вблизи)
    float    Pad[5];
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
