#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <memory>
#include "RenderingSystem.h"
#include "InputDevice.h"
#include "Types.h"

struct Projectile { XMFLOAT3 position; XMFLOAT3 velocity; float distTraveled; bool stuck; };
struct TriGrid { float cellSize=2; XMFLOAT3 origin={}; int nx=0,ny=0,nz=0; std::vector<std::vector<uint32_t>> cells; void Build(const std::vector<XMFLOAT3>&); void Query(XMFLOAT3,float,std::vector<uint32_t>&) const; bool Empty() const{return cells.empty();} };
struct AABB { XMFLOAT3 min; XMFLOAT3 max; };
struct SceneObject { XMFLOAT3 position; float scale; AABB worldAABB; bool isStatic; XMFLOAT3 basePosition; float orbitRadius,orbitSpeed,orbitPhase,bobSpeed,bobAmplitude; };
struct Plane { float a,b,c,d; };
struct Frustum { Plane planes[6]; };
struct OctreeNode { AABB bounds; std::vector<int> indices; std::unique_ptr<OctreeNode> children[8]; bool isLeaf=true; };

class Octree {
public: void Build(const std::vector<SceneObject>&,const std::vector<int>&,int=7,int=8); void QueryFrustum(const Frustum&,std::vector<int>&) const; bool Empty() const{return !root_;}
private: void BuildRecursive(OctreeNode*,const std::vector<SceneObject>&,const std::vector<int>&,int,int,int); void Subdivide(OctreeNode*); void QueryNode(const OctreeNode*,const Frustum&,std::vector<int>&) const; static bool AABBInFrustum(const AABB&,const Frustum&); std::unique_ptr<OctreeNode> root_; };

class Game {
public: Game(HWND,int,int); ~Game()=default; bool Initialize(); void Update(float,InputDevice*); void Render(); void Resize(int,int);
private:
    void SetupLights(); void BuildGeometryCB(GeometryCBData&,TessImportance=TessImportance::Medium) const;
    void BuildGeometryCBForObject(GeometryCBData&,const SceneObject&) const;
    void BuildGeometryCBForSphere(GeometryCBData&,XMFLOAT3,float) const;
    void BuildLightingCB(LightingCBData&) const;
    void BuildParticleCB(ParticleCBData&,float,uint32_t) const;
    void Shoot(); Frustum BuildFrustum() const; void UpdateTitle();
    void UpdateDynamicObjects(float); void RecalcAABB(SceneObject&);

    // Cascaded Shadow Maps
    struct CascadeData {
        XMMATRIX lightVP[CASCADE_COUNT];
        float    splitFar[CASCADE_COUNT];
        XMFLOAT3 lightDir;
        XMFLOAT3 sphereCenter[CASCADE_COUNT];   // world-space center of cascade frustum slice
        float    sphereRadius[CASCADE_COUNT];   // bounding sphere radius
    };
    void ComputeCascades(CascadeData& out) const;
    void BuildShadowCBForObject(ShadowCBData&, const SceneObject&, const XMMATRIX& lightVP) const;
    void BuildShadowCBForSphere(ShadowCBData&, XMFLOAT3 pos, float scale, const XMMATRIX& lightVP) const;
    void BuildShadowCBForScene(ShadowCBData&, const XMMATRIX& lightVP) const;

    std::unique_ptr<RenderingSystem> rs_;
    std::vector<LightData> lights_; std::vector<Projectile> projectiles_;
    std::vector<XMFLOAT3> triSoup_; TriGrid collGrid_; std::vector<uint32_t> queryBuf_;
    std::vector<SceneObject> sceneObjects_; std::vector<int> staticIndices_,dynamicIndices_;
    Octree octree_;
    bool frustumCulling_=true, useOctree_=true;
    int visibleStatic_=0, visibleDynamic_=0, frameNumber_=0, octreeRebuilds_=0, dynamicCullCalls_=0;

    // Big fountain emitter
    XMFLOAT3 particleEmitPos_ = { 0.0f, 1.5f, 0.0f };
    uint32_t particleEmitRate_ = 30;   // ~30/frame * 6s lifetime * 60fps ≈ 10800 — clamped to MAX_GPU_PARTICLES

    float time_=0, uvOffsetX_=0, uvOffsetY_=0;
    float camX_=0, camY_=5, camZ_=-15;
    float camYaw_=0, camPitch_=0;
    float moveSpeed_=20, mouseSensitivity_=0.003f, nearPlane_=0.5f, farPlane_=5000;
    float projectileSpeed_=80, projectileMaxDist_=400, projectileRadius_=0.4f;
    bool prevSpace_=false, prevF1_=false, prevG_=false, prevH_=false, wireframe_=false;
    int width_,height_; HWND hwnd_;
};
