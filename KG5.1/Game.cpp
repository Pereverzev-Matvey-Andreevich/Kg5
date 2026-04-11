#include "Game.h"
#include "TextureLoader.h"
#include "ObjLoader.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

void TriGrid::Build(const std::vector<XMFLOAT3>& soup)
{
    cells.clear();
    nx = ny = nz = 0;
    if (soup.size() < 3) return;

    float mnx = soup[0].x, mny = soup[0].y, mnz = soup[0].z;
    float mxx = mnx,       mxy = mny,       mxz = mnz;
    for (const auto& v : soup)
    {
        mnx = fminf(mnx, v.x); mny = fminf(mny, v.y); mnz = fminf(mnz, v.z);
        mxx = fmaxf(mxx, v.x); mxy = fmaxf(mxy, v.y); mxz = fmaxf(mxz, v.z);
    }

    float extent = fmaxf(fmaxf(mxx - mnx, mxy - mny), mxz - mnz);
    cellSize = fmaxf(1.0f, extent / 64.0f);

    float pad = cellSize;
    origin = { mnx - pad, mny - pad, mnz - pad };
    nx = (int)ceilf((mxx - mnx + 2.0f * pad) / cellSize) + 1;
    ny = (int)ceilf((mxy - mny + 2.0f * pad) / cellSize) + 1;
    nz = (int)ceilf((mxz - mnz + 2.0f * pad) / cellSize) + 1;

    cells.assign((size_t)nx * ny * nz, {});

    uint32_t triCount = (uint32_t)(soup.size() / 3);
    for (uint32_t i = 0; i < triCount; ++i)
    {
        const XMFLOAT3& a = soup[i * 3 + 0];
        const XMFLOAT3& b = soup[i * 3 + 1];
        const XMFLOAT3& c = soup[i * 3 + 2];

        float tmnx = fminf(fminf(a.x, b.x), c.x);
        float tmny = fminf(fminf(a.y, b.y), c.y);
        float tmnz = fminf(fminf(a.z, b.z), c.z);
        float tmxx = fmaxf(fmaxf(a.x, b.x), c.x);
        float tmxy = fmaxf(fmaxf(a.y, b.y), c.y);
        float tmxz = fmaxf(fmaxf(a.z, b.z), c.z);

        int x0 = (int)((tmnx - origin.x) / cellSize);
        int y0 = (int)((tmny - origin.y) / cellSize);
        int z0 = (int)((tmnz - origin.z) / cellSize);
        int x1 = (int)((tmxx - origin.x) / cellSize);
        int y1 = (int)((tmxy - origin.y) / cellSize);
        int z1 = (int)((tmxz - origin.z) / cellSize);

        x0 = (x0 < 0) ? 0 : (x0 >= nx ? nx - 1 : x0);
        y0 = (y0 < 0) ? 0 : (y0 >= ny ? ny - 1 : y0);
        z0 = (z0 < 0) ? 0 : (z0 >= nz ? nz - 1 : z0);
        x1 = (x1 < 0) ? 0 : (x1 >= nx ? nx - 1 : x1);
        y1 = (y1 < 0) ? 0 : (y1 >= ny ? ny - 1 : y1);
        z1 = (z1 < 0) ? 0 : (z1 >= nz ? nz - 1 : z1);

        for (int iz = z0; iz <= z1; ++iz)
        for (int iy = y0; iy <= y1; ++iy)
        for (int ix = x0; ix <= x1; ++ix)
            cells[(size_t)(iz * ny + iy) * nx + ix].push_back(i);
    }
}

void TriGrid::Query(XMFLOAT3 pos, float r, std::vector<uint32_t>& out) const
{
    if (cells.empty()) return;

    int x0 = (int)((pos.x - r - origin.x) / cellSize);
    int y0 = (int)((pos.y - r - origin.y) / cellSize);
    int z0 = (int)((pos.z - r - origin.z) / cellSize);
    int x1 = (int)((pos.x + r - origin.x) / cellSize);
    int y1 = (int)((pos.y + r - origin.y) / cellSize);
    int z1 = (int)((pos.z + r - origin.z) / cellSize);

    x0 = (x0 < 0) ? 0 : (x0 >= nx ? nx - 1 : x0);
    y0 = (y0 < 0) ? 0 : (y0 >= ny ? ny - 1 : y0);
    z0 = (z0 < 0) ? 0 : (z0 >= nz ? nz - 1 : z0);
    x1 = (x1 < 0) ? 0 : (x1 >= nx ? nx - 1 : x1);
    y1 = (y1 < 0) ? 0 : (y1 >= ny ? ny - 1 : y1);
    z1 = (z1 < 0) ? 0 : (z1 >= nz ? nz - 1 : z1);

    for (int iz = z0; iz <= z1; ++iz)
    for (int iy = y0; iy <= y1; ++iy)
    for (int ix = x0; ix <= x1; ++ix)
    {
        for (uint32_t ti : cells[(size_t)(iz * ny + iy) * nx + ix])
        {
            bool dup = false;
            for (uint32_t o : out) { if (o == ti) { dup = true; break; } }
            if (!dup) out.push_back(ti);
        }
    }
}

bool Octree::AABBInFrustum(const AABB& aabb, const Frustum& f)
{
    for (int i = 0; i < 6; ++i)
    {
        const Plane& p = f.planes[i];
        float px = (p.a > 0.0f) ? aabb.max.x : aabb.min.x;
        float py = (p.b > 0.0f) ? aabb.max.y : aabb.min.y;
        float pz = (p.c > 0.0f) ? aabb.max.z : aabb.min.z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f)
            return false;
    }
    return true;
}

void Octree::Subdivide(OctreeNode* node)
{
    XMFLOAT3 mn  = node->bounds.min;
    XMFLOAT3 mx  = node->bounds.max;
    XMFLOAT3 mid = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };

    for (int i = 0; i < 8; ++i)
    {
        node->children[i] = std::make_unique<OctreeNode>();
        node->children[i]->isLeaf  = true;
        node->children[i]->bounds.min = {
            (i & 1) ? mid.x : mn.x,
            (i & 2) ? mid.y : mn.y,
            (i & 4) ? mid.z : mn.z
        };
        node->children[i]->bounds.max = {
            (i & 1) ? mx.x : mid.x,
            (i & 2) ? mx.y : mid.y,
            (i & 4) ? mx.z : mid.z
        };
    }
}

void Octree::BuildRecursive(OctreeNode* node,
                             const std::vector<SceneObject>& objects,
                             const std::vector<int>& indices,
                             int depth, int maxDepth, int maxPerNode)
{
    if ((int)indices.size() <= maxPerNode || depth >= maxDepth)
    {
        node->indices = indices;
        node->isLeaf  = true;
        return;
    }

    node->isLeaf = false;
    Subdivide(node);

    XMFLOAT3 mid = {
        (node->bounds.min.x + node->bounds.max.x) * 0.5f,
        (node->bounds.min.y + node->bounds.max.y) * 0.5f,
        (node->bounds.min.z + node->bounds.max.z) * 0.5f
    };

    std::vector<int> childBuckets[8];
    for (int idx : indices)
    {
        const XMFLOAT3& pos = objects[idx].position;
        int c = ((pos.x >= mid.x) ? 1 : 0)
              | ((pos.y >= mid.y) ? 2 : 0)
              | ((pos.z >= mid.z) ? 4 : 0);
        childBuckets[c].push_back(idx);
    }

    for (int i = 0; i < 8; ++i)
        BuildRecursive(node->children[i].get(), objects, childBuckets[i],
                       depth + 1, maxDepth, maxPerNode);
}

void Octree::Build(const std::vector<SceneObject>& objects, int maxDepth, int maxPerNode)
{
    root_.reset();
    if (objects.empty()) return;

    AABB bounds;
    bounds.min = objects[0].worldAABB.min;
    bounds.max = objects[0].worldAABB.max;
    for (const auto& o : objects)
    {
        bounds.min.x = fminf(bounds.min.x, o.worldAABB.min.x);
        bounds.min.y = fminf(bounds.min.y, o.worldAABB.min.y);
        bounds.min.z = fminf(bounds.min.z, o.worldAABB.min.z);
        bounds.max.x = fmaxf(bounds.max.x, o.worldAABB.max.x);
        bounds.max.y = fmaxf(bounds.max.y, o.worldAABB.max.y);
        bounds.max.z = fmaxf(bounds.max.z, o.worldAABB.max.z);
    }

    root_ = std::make_unique<OctreeNode>();
    root_->bounds = bounds;
    root_->isLeaf = true;

    std::vector<int> allIndices;
    allIndices.reserve(objects.size());
    for (int i = 0; i < (int)objects.size(); ++i)
        allIndices.push_back(i);

    BuildRecursive(root_.get(), objects, allIndices, 0, maxDepth, maxPerNode);
}

void Octree::QueryNode(const OctreeNode* node, const Frustum& f, std::vector<int>& out) const
{
    if (!AABBInFrustum(node->bounds, f)) return;

    for (int idx : node->indices)
        out.push_back(idx);

    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i)
            QueryNode(node->children[i].get(), f, out);
}

void Octree::QueryFrustum(const Frustum& f, std::vector<int>& out) const
{
    if (!root_) return;
    QueryNode(root_.get(), f, out);
}

static XMVECTOR ClosestPointOnTriangle(XMVECTOR P,
                                        XMVECTOR A, XMVECTOR B, XMVECTOR C)
{
    XMVECTOR AB = XMVectorSubtract(B, A);
    XMVECTOR AC = XMVectorSubtract(C, A);
    XMVECTOR AP = XMVectorSubtract(P, A);

    float d1 = XMVectorGetX(XMVector3Dot(AB, AP));
    float d2 = XMVectorGetX(XMVector3Dot(AC, AP));
    if (d1 <= 0.0f && d2 <= 0.0f) return A;

    XMVECTOR BP = XMVectorSubtract(P, B);
    float d3 = XMVectorGetX(XMVector3Dot(AB, BP));
    float d4 = XMVectorGetX(XMVector3Dot(AC, BP));
    if (d3 >= 0.0f && d4 <= d3) return B;

    XMVECTOR CP = XMVectorSubtract(P, C);
    float d5 = XMVectorGetX(XMVector3Dot(AB, CP));
    float d6 = XMVectorGetX(XMVector3Dot(AC, CP));
    if (d6 >= 0.0f && d5 <= d6) return C;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        float v = d1 / (d1 - d3);
        return XMVectorAdd(A, XMVectorScale(AB, v));
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        float w = d2 / (d2 - d6);
        return XMVectorAdd(A, XMVectorScale(AC, w));
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return XMVectorAdd(B, XMVectorScale(XMVectorSubtract(C, B), w));
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return XMVectorAdd(A, XMVectorAdd(XMVectorScale(AB, v), XMVectorScale(AC, w)));
}

static bool AABBInFrustumBrute(const AABB& aabb, const Frustum& f)
{
    for (int i = 0; i < 6; ++i)
    {
        const Plane& p = f.planes[i];
        float px = (p.a > 0.0f) ? aabb.max.x : aabb.min.x;
        float py = (p.b > 0.0f) ? aabb.max.y : aabb.min.y;
        float pz = (p.c > 0.0f) ? aabb.max.z : aabb.min.z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f)
            return false;
    }
    return true;
}

Game::Game(HWND hwnd, int width, int height)
    : hwnd_(hwnd), width_(width), height_(height)
{
    rs_ = std::make_unique<RenderingSystem>(hwnd, width, height);
}

bool Game::Initialize()
{
    if (!rs_->Initialize())
        return false;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);

    const wchar_t* modelCandidates[] = { L"sponza.obj", L"Sponza.obj", L"model.obj" };
    bool modelLoaded = false;
    for (auto* name : modelCandidates)
    {
        std::wstring full = dir + name;
        if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            rs_->LoadMeshFromObj(full);

            ObjResult obj = LoadObj(full);
            if (obj.valid)
            {
                triSoup_.reserve(obj.indices.size());
                for (UINT idx : obj.indices)
                    triSoup_.push_back(obj.vertices[idx].Position);
            }

            modelLoaded = true;
            break;
        }
    }
    if (!modelLoaded)
    {
        rs_->LoadMeshFromBuiltin();

        float s = 0.5f;
        struct F { XMFLOAT3 v[4]; };
        F faces[] = {
            {{{-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s}}},
            {{{ s,-s,-s},{-s,-s,-s},{-s, s,-s},{ s, s,-s}}},
            {{{-s,-s,-s},{-s,-s, s},{-s, s, s},{-s, s,-s}}},
            {{{ s,-s, s},{ s,-s,-s},{ s, s,-s},{ s, s, s}}},
            {{{-s, s, s},{ s, s, s},{ s, s,-s},{-s, s,-s}}},
            {{{-s,-s,-s},{ s,-s,-s},{ s,-s, s},{-s,-s, s}}},
        };
        for (auto& f : faces)
        {
            triSoup_.push_back(f.v[0]); triSoup_.push_back(f.v[1]); triSoup_.push_back(f.v[2]);
            triSoup_.push_back(f.v[0]); triSoup_.push_back(f.v[2]); triSoup_.push_back(f.v[3]);
        }
    }

    collGrid_.Build(triSoup_);

    {
        TextureData td = LoadTextureWIC(dir + L"texture1.png");
        if (!td.valid) td = LoadTextureWIC(dir + L"texture1.jpg");
        if (!td.valid) td = CreateCheckerboard(256, 32);
        rs_->LoadTexture(td, 0);
    }
    {
        TextureData td = LoadTextureWIC(dir + L"texture2.png");
        if (!td.valid) td = LoadTextureWIC(dir + L"texture2.jpg");
        if (!td.valid) td = CreateCheckerboard(256, 16);
        rs_->LoadTexture(td, 1);
    }
    {
        TextureData td = LoadTextureWIC(dir + L"displacement.png");
        if (!td.valid) td = LoadTextureWIC(dir + L"displacement.jpg");
        if (!td.valid)
        {
            const UINT sz = 256;
            td.width = sz; td.height = sz; td.valid = true;
            td.pixels.resize(sz * sz * 4);
            for (UINT y = 0; y < sz; ++y)
            for (UINT x = 0; x < sz; ++x)
            {
                float fx = (float)x / sz;
                float fy = (float)y / sz;
                float h  = 0.5f + 0.5f * sinf(fx * 3.14159f * 8.0f) * cosf(fy * 3.14159f * 8.0f);
                uint8_t v = (uint8_t)(h * 255.0f);
                UINT i = (y * sz + x) * 4;
                td.pixels[i+0] = v; td.pixels[i+1] = v;
                td.pixels[i+2] = v; td.pixels[i+3] = 255;
            }
        }
        rs_->LoadTexture(td, 2);
    }
    {
        TextureData td = LoadTextureWIC(dir + L"normal.png");
        if (!td.valid) td = LoadTextureWIC(dir + L"normal.jpg");
        if (!td.valid)
        {
            const UINT sz = 256;
            td.width = sz; td.height = sz; td.valid = true;
            td.pixels.resize(sz * sz * 4, 0);
            for (UINT i = 0; i < sz * sz; ++i)
            {
                td.pixels[i*4+0] = 128;
                td.pixels[i*4+1] = 128;
                td.pixels[i*4+2] = 255;
                td.pixels[i*4+3] = 255;
            }
        }
        rs_->LoadTexture(td, 3);
    }

    srand(1337);
    const int NUM_OBJECTS = 1000;
    sceneObjects_.reserve(NUM_OBJECTS);
    for (int i = 0; i < NUM_OBJECTS; ++i)
    {
        auto rng = [](float lo, float hi) -> float
        {
            return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
        };

        SceneObject obj;
        obj.position = { rng(-20.0f, 20.0f), rng(0.3f, 14.0f), rng(-7.0f, 7.0f) };
        obj.scale    = rng(0.2f, 0.6f);
        obj.worldAABB.min = { obj.position.x - obj.scale,
                              obj.position.y - obj.scale,
                              obj.position.z - obj.scale };
        obj.worldAABB.max = { obj.position.x + obj.scale,
                              obj.position.y + obj.scale,
                              obj.position.z + obj.scale };
        sceneObjects_.push_back(obj);
    }

    octree_.Build(sceneObjects_);

    SetupLights();
    return true;
}

void Game::SetupLights()
{
    lights_.clear();

    {
        LightData l = {};
        l.Type     = static_cast<int>(LightType::Point);
        l.Position = { -8.0f, 3.0f, 0.0f, 1.0f };
        l.Color    = { 1.0f, 0.8f, 0.6f, 8.0f };
        l.Range    = 50.0f;
        lights_.push_back(l);
    }
    {
        LightData l = {};
        l.Type      = static_cast<int>(LightType::Directional);
        l.Direction = { 0.5f, -1.0f, 0.5f, 0.0f };
        l.Color     = { 1.0f, 1.0f, 1.0f, 1.5f };
        lights_.push_back(l);
    }
    {
        LightData l = {};
        l.Type           = static_cast<int>(LightType::Spot);
        l.Position       = { 8.0f, 8.0f, 0.0f, 1.0f };
        l.Direction      = { 0.0f, -1.0f, 0.0f, 0.0f };
        l.Color          = { 0.8f, 0.9f, 1.0f, 10.0f };
        l.Range          = 50.0f;
        l.InnerConeAngle = XMConvertToRadians(20.0f);
        l.OuterConeAngle = XMConvertToRadians(50.0f);
        lights_.push_back(l);
    }
}

void Game::Shoot()
{
    float cosPitch = cosf(camPitch_);
    XMFLOAT3 dir = {
        sinf(camYaw_)  * cosPitch,
       -sinf(camPitch_),
        cosf(camYaw_)  * cosPitch
    };

    Projectile p;
    p.position     = { camX_, camY_, camZ_ };
    p.velocity     = { dir.x * projectileSpeed_,
                       dir.y * projectileSpeed_,
                       dir.z * projectileSpeed_ };
    p.distTraveled = 0.0f;
    p.stuck        = false;
    projectiles_.push_back(p);
}

void Game::UpdateTitle()
{
    wchar_t buf[256];
    const wchar_t* fc     = frustumCulling_ ? L"ON"  : L"OFF";
    const wchar_t* octree = useOctree_      ? L"ON"  : L"OFF";
    int total = (int)sceneObjects_.size();
    swprintf_s(buf, L"FC:%s  OCTREE:%s  VISIBLE:%d/%d  [G]-toggle FC  [H]-toggle Octree",
               fc, octree, visibleCount_, total);
    SetWindowTextW(hwnd_, buf);
}

Frustum Game::BuildFrustum() const
{
    XMVECTOR eye = XMVectorSet(camX_, camY_, camZ_, 1.0f);
    float cp = cosf(camPitch_);
    XMVECTOR fwd = XMVectorSet(sinf(camYaw_) * cp, -sinf(camPitch_),
                               cosf(camYaw_) * cp, 0.0f);
    XMVECTOR up  = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorAdd(eye, fwd), up);

    float aspect = (rs_->GetHeight() > 0)
        ? (float)rs_->GetWidth() / (float)rs_->GetHeight() : 1.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(60.0f), aspect, nearPlane_, farPlane_);

    XMMATRIX vp = view * proj;

    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, vp);

    Frustum f;
    f.planes[0] = { m._11 + m._14, m._21 + m._24, m._31 + m._34, m._41 + m._44 };
    f.planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
    f.planes[2] = { m._12 + m._14, m._22 + m._24, m._32 + m._34, m._42 + m._44 };
    f.planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
    f.planes[4] = { m._13,         m._23,         m._33,         m._43         };
    f.planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };
    return f;
}

void Game::Update(float deltaTime, InputDevice* input)
{
    if (deltaTime > 0.033f) deltaTime = 0.033f;

    time_ += deltaTime;

    uvOffsetX_ += 0.03f * deltaTime;
    uvOffsetY_ += 0.01f * deltaTime;
    if (uvOffsetX_ > 1.0f) uvOffsetX_ -= 1.0f;
    if (uvOffsetY_ > 1.0f) uvOffsetY_ -= 1.0f;

    if (input)
    {
        if (input->IsMouseDown(1))
        {
            camYaw_   += input->GetMouseDeltaX() * mouseSensitivity_;
            camPitch_ += input->GetMouseDeltaY() * mouseSensitivity_;
            const float limit = XMConvertToRadians(89.0f);
            if (camPitch_ >  limit) camPitch_ =  limit;
            if (camPitch_ < -limit) camPitch_ = -limit;
        }

        float cosPitch = cosf(camPitch_);
        float fx = sinf(camYaw_) * cosPitch;
        float fy = -sinf(camPitch_);
        float fz = cosf(camYaw_) * cosPitch;

        float rx =  cosf(camYaw_);
        float ry =  0.0f;
        float rz = -sinf(camYaw_);

        float speed = moveSpeed_ * deltaTime;
        if (input->IsKeyDown(VK_SHIFT)) speed *= 3.0f;

        if (input->IsKeyDown('W')) { camX_ += fx * speed; camY_ += fy * speed; camZ_ += fz * speed; }
        if (input->IsKeyDown('S')) { camX_ -= fx * speed; camY_ -= fy * speed; camZ_ -= fz * speed; }
        if (input->IsKeyDown('D')) { camX_ += rx * speed; camY_ += ry * speed; camZ_ += rz * speed; }
        if (input->IsKeyDown('A')) { camX_ -= rx * speed; camY_ -= ry * speed; camZ_ -= rz * speed; }
        if (input->IsKeyDown('E')) { camY_ += speed; }
        if (input->IsKeyDown('Q')) { camY_ -= speed; }

        bool spaceNow = input->IsKeyDown(VK_SPACE);
        if (spaceNow && !prevSpace_)
            Shoot();
        prevSpace_ = spaceNow;

        bool f1Now = input->IsKeyDown('F');
        if (f1Now && !prevF1_)
        {
            wireframe_ = !wireframe_;
            rs_->SetWireframe(wireframe_);
        }
        prevF1_ = f1Now;

        bool gNow = input->IsKeyDown('G');
        if (gNow && !prevG_)
            frustumCulling_ = !frustumCulling_;
        prevG_ = gNow;

        bool hNow = input->IsKeyDown('H');
        if (hNow && !prevH_)
            useOctree_ = !useOctree_;
        prevH_ = hNow;
    }

    const float r       = projectileRadius_;
    const float r2      = r * r;
    const float subStep = r * 0.5f;

    for (auto& p : projectiles_)
    {
        if (p.stuck) continue;

        float moveDist = projectileSpeed_ * deltaTime;
        p.distTraveled += moveDist;
        if (p.distTraveled >= projectileMaxDist_)
        {
            p.stuck = true;
            continue;
        }

        float invSpd = 1.0f / projectileSpeed_;
        XMFLOAT3 dirF = { p.velocity.x * invSpd,
                           p.velocity.y * invSpd,
                           p.velocity.z * invSpd };
        XMVECTOR dir = XMLoadFloat3(&dirF);

        float remaining = moveDist;
        while (remaining > 0.0f)
        {
            float step = (remaining < subStep) ? remaining : subStep;
            remaining -= step;

            XMVECTOR pos = XMLoadFloat3(&p.position);
            pos = XMVectorAdd(pos, XMVectorScale(dir, step));
            XMStoreFloat3(&p.position, pos);

            if (collGrid_.Empty()) continue;

            queryBuf_.clear();
            collGrid_.Query(p.position, r, queryBuf_);

            bool hit = false;
            for (uint32_t ti : queryBuf_)
            {
                XMVECTOR A = XMLoadFloat3(&triSoup_[ti * 3 + 0]);
                XMVECTOR B = XMLoadFloat3(&triSoup_[ti * 3 + 1]);
                XMVECTOR C = XMLoadFloat3(&triSoup_[ti * 3 + 2]);

                XMVECTOR closest = ClosestPointOnTriangle(pos, A, B, C);
                XMVECTOR diff    = XMVectorSubtract(pos, closest);
                float    dist2   = XMVectorGetX(XMVector3Dot(diff, diff));

                if (dist2 <= r2)
                {
                    if (dist2 > 1e-8f)
                    {
                        float    dist   = sqrtf(dist2);
                        XMVECTOR normal = XMVectorScale(diff, 1.0f / dist);
                        pos = XMVectorAdd(closest, XMVectorScale(normal, r));
                        XMStoreFloat3(&p.position, pos);
                    }
                    p.stuck = true;
                    hit     = true;
                    break;
                }
            }

            if (hit) break;
        }
    }
}

void Game::BuildGeometryCB(GeometryCBData& cb, TessImportance importance) const
{
    XMMATRIX world = XMMatrixIdentity();
    XMVECTOR eye   = XMVectorSet(camX_, camY_, camZ_, 1.0f);
    float cosPitch = cosf(camPitch_);
    XMVECTOR forward = XMVectorSet(
        sinf(camYaw_) * cosPitch, -sinf(camPitch_),
        cosf(camYaw_) * cosPitch, 0.0f);
    XMVECTOR target = XMVectorAdd(eye, forward);
    XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view   = XMMatrixLookAtLH(eye, target, up);
    float aspect = (rs_->GetHeight() > 0)
        ? static_cast<float>(rs_->GetWidth()) / rs_->GetHeight() : 1.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(60.0f), aspect, nearPlane_, farPlane_);

    float tessMin = 1.0f, tessMax = 1.0f;
    TessImportanceToRange(importance, tessMin, tessMax);

    cb = {};
    cb.World             = XMMatrixTranspose(world);
    cb.View              = XMMatrixTranspose(view);
    cb.Proj              = XMMatrixTranspose(proj);
    cb.Tiling            = XMFLOAT2(4.0f, 4.0f);
    cb.UVOffset          = XMFLOAT2(uvOffsetX_, uvOffsetY_);
    cb.CameraPos         = XMFLOAT4(camX_, camY_, camZ_, 1.0f);
    cb.DisplacementScale = 1.0f;
    cb.TessMin           = tessMin;
    cb.TessMax           = tessMax;
}

void Game::BuildGeometryCBForSphere(GeometryCBData& out, XMFLOAT3 pos) const
{
    BuildGeometryCB(out, TessImportance::None);
    XMMATRIX world = XMMatrixScaling(projectileRadius_, projectileRadius_, projectileRadius_)
                   * XMMatrixTranslation(pos.x, pos.y, pos.z);
    out.World             = XMMatrixTranspose(world);
    out.DisplacementScale = 0.0f;
}

void Game::BuildLightingCB(LightingCBData& cb) const
{
    cb = {};
    cb.CameraPos = XMFLOAT4(camX_, camY_, camZ_, 1.0f);

    std::vector<LightData> allLights = lights_;

    auto addProjectileLight = [&](const Projectile& p)
    {
        if ((int)allLights.size() >= MAX_LIGHTS) return;
        LightData l = {};
        l.Type     = static_cast<int>(LightType::Point);
        l.Position = { p.position.x, p.position.y, p.position.z, 1.0f };
        l.Color    = { 0.7f, 0.0f, 1.0f, 20.0f };
        l.Range    = 30.0f;
        allLights.push_back(l);
    };

    for (auto& p : projectiles_)
        if (p.stuck)  addProjectileLight(p);

    for (auto& p : projectiles_)
        if (!p.stuck) addProjectileLight(p);

    cb.LightCount = static_cast<int>(allLights.size());
    int count = (int)allLights.size();
    if (count > MAX_LIGHTS) count = MAX_LIGHTS;
    for (int i = 0; i < count; ++i)
        cb.Lights[i] = allLights[i];
}

void Game::Render()
{
    LightingCBData lightCB;
    BuildLightingCB(lightCB);

    Frustum frustum = BuildFrustum();

    std::vector<int> visibleIndices;
    visibleIndices.reserve(sceneObjects_.size());

    if (!frustumCulling_)
    {
        for (int i = 0; i < (int)sceneObjects_.size(); ++i)
            visibleIndices.push_back(i);
    }
    else if (useOctree_)
    {
        octree_.QueryFrustum(frustum, visibleIndices);
    }
    else
    {
        for (int i = 0; i < (int)sceneObjects_.size(); ++i)
            if (AABBInFrustumBrute(sceneObjects_[i].worldAABB, frustum))
                visibleIndices.push_back(i);
    }

    visibleCount_ = (int)visibleIndices.size();
    UpdateTitle();

    rs_->BeginFrame();
    rs_->BeginGeometryPass();

    {
        GeometryCBData geomCB;
        BuildGeometryCB(geomCB, TessImportance::High);
        rs_->DrawSceneMeshTess(geomCB);
    }

    for (int idx : visibleIndices)
    {
        const SceneObject& obj = sceneObjects_[idx];
        GeometryCBData cb;
        BuildGeometryCB(cb, TessImportance::None);
        XMMATRIX world = XMMatrixScaling(obj.scale, obj.scale, obj.scale)
                       * XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
        cb.World             = XMMatrixTranspose(world);
        cb.DisplacementScale = 0.0f;
        rs_->DrawSphere(cb);
    }

    for (auto& p : projectiles_)
    {
        GeometryCBData sphereCB;
        BuildGeometryCBForSphere(sphereCB, p.position);
        rs_->DrawSphere(sphereCB);
    }

    rs_->EndGeometryPass();
    rs_->DoLightingPass(lightCB);
    rs_->EndFrame();
}

void Game::Resize(int width, int height)
{
    width_  = width;
    height_ = height;
    rs_->Resize(width, height);
}
