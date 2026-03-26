#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <memory>
#include "RenderingSystem.h"
#include "InputDevice.h"
#include "Types.h"

struct Projectile
{
    XMFLOAT3 position;
    XMFLOAT3 velocity;
    float    distTraveled;
    bool     stuck;
};

struct TriGrid
{
    float    cellSize = 2.0f;
    XMFLOAT3 origin   = {};
    int      nx = 0, ny = 0, nz = 0;

    std::vector<std::vector<uint32_t>> cells;

    void Build(const std::vector<XMFLOAT3>& soup);

    void Query(XMFLOAT3 pos, float r, std::vector<uint32_t>& out) const;

    bool Empty() const { return cells.empty(); }
};

class Game
{
public:
    Game(HWND hwnd, int width, int height);
    ~Game() = default;

    bool Initialize();
    void Update(float deltaTime, InputDevice* input);
    void Render();
    void Resize(int width, int height);

private:
    void SetupLights();

    // importance — пресет тесселяции для данного draw call.
    // TessImportance::High   -> TessMax=32  (детали, колонны Sponza)
    // TessImportance::Medium -> TessMax=16  (стены, полы)
    // TessImportance::Low    -> TessMax=4   (потолки, задний план)
    // TessImportance::None   -> TessMax=1   (снаряды, дебаг-объекты)
    void BuildGeometryCB(GeometryCBData& out,
                         TessImportance importance = TessImportance::Medium) const;

    void BuildGeometryCBForSphere(GeometryCBData& out, XMFLOAT3 pos) const;
    void BuildLightingCB(LightingCBData& out) const;
    void Shoot();

    std::unique_ptr<RenderingSystem> rs_;
    std::vector<LightData>           lights_;
    std::vector<Projectile>          projectiles_;

    std::vector<XMFLOAT3> triSoup_;
    TriGrid               collGrid_;
    std::vector<uint32_t> queryBuf_;

    float time_      = 0.0f;
    float uvOffsetX_ = 0.0f;
    float uvOffsetY_ = 0.0f;

    float camX_ = 0.0f;
    float camY_ = 5.0f;
    float camZ_ = -15.0f;

    float camYaw_   = 0.0f;
    float camPitch_ = 0.0f;

    float moveSpeed_        = 20.0f;
    float mouseSensitivity_ = 0.003f;
    float nearPlane_        = 0.5f;
    float farPlane_         = 5000.0f;

    float projectileSpeed_   = 80.0f;
    float projectileMaxDist_ = 400.0f;
    float projectileRadius_  = 0.4f;

    bool prevSpace_ = false;
    bool prevF1_    = false;
    bool wireframe_ = false;

    int  width_;
    int  height_;
    HWND hwnd_;
};
