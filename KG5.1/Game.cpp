#include "Game.h"
#include "TextureLoader.h"
#include "ObjLoader.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

void TriGrid::Build(const std::vector<XMFLOAT3>& soup){cells.clear();nx=ny=nz=0;if(soup.size()<3)return;float mnx=soup[0].x,mny=soup[0].y,mnz=soup[0].z;float mxx=mnx,mxy=mny,mxz=mnz;for(const auto&v:soup){mnx=fminf(mnx,v.x);mny=fminf(mny,v.y);mnz=fminf(mnz,v.z);mxx=fmaxf(mxx,v.x);mxy=fmaxf(mxy,v.y);mxz=fmaxf(mxz,v.z);}float ext=fmaxf(fmaxf(mxx-mnx,mxy-mny),mxz-mnz);cellSize=fmaxf(1.0f,ext/64.0f);float pad=cellSize;origin={mnx-pad,mny-pad,mnz-pad};nx=(int)ceilf((mxx-mnx+2*pad)/cellSize)+1;ny=(int)ceilf((mxy-mny+2*pad)/cellSize)+1;nz=(int)ceilf((mxz-mnz+2*pad)/cellSize)+1;cells.assign((size_t)nx*ny*nz,{});uint32_t tc=(uint32_t)(soup.size()/3);for(uint32_t i=0;i<tc;++i){const auto&a=soup[i*3],&b=soup[i*3+1],&c=soup[i*3+2];float tmnx=fminf(fminf(a.x,b.x),c.x),tmny=fminf(fminf(a.y,b.y),c.y),tmnz=fminf(fminf(a.z,b.z),c.z);float tmxx=fmaxf(fmaxf(a.x,b.x),c.x),tmxy=fmaxf(fmaxf(a.y,b.y),c.y),tmxz=fmaxf(fmaxf(a.z,b.z),c.z);int x0=(int)((tmnx-origin.x)/cellSize),y0=(int)((tmny-origin.y)/cellSize),z0=(int)((tmnz-origin.z)/cellSize);int x1=(int)((tmxx-origin.x)/cellSize),y1=(int)((tmxy-origin.y)/cellSize),z1=(int)((tmxz-origin.z)/cellSize);x0=max(0,min(x0,nx-1));y0=max(0,min(y0,ny-1));z0=max(0,min(z0,nz-1));x1=max(0,min(x1,nx-1));y1=max(0,min(y1,ny-1));z1=max(0,min(z1,nz-1));for(int iz=z0;iz<=z1;++iz)for(int iy=y0;iy<=y1;++iy)for(int ix=x0;ix<=x1;++ix)cells[(size_t)(iz*ny+iy)*nx+ix].push_back(i);}}
void TriGrid::Query(XMFLOAT3 pos,float r,std::vector<uint32_t>&out)const{if(cells.empty())return;int x0=(int)((pos.x-r-origin.x)/cellSize),y0=(int)((pos.y-r-origin.y)/cellSize),z0=(int)((pos.z-r-origin.z)/cellSize);int x1=(int)((pos.x+r-origin.x)/cellSize),y1=(int)((pos.y+r-origin.y)/cellSize),z1=(int)((pos.z+r-origin.z)/cellSize);x0=max(0,min(x0,nx-1));y0=max(0,min(y0,ny-1));z0=max(0,min(z0,nz-1));x1=max(0,min(x1,nx-1));y1=max(0,min(y1,ny-1));z1=max(0,min(z1,nz-1));for(int iz=z0;iz<=z1;++iz)for(int iy=y0;iy<=y1;++iy)for(int ix=x0;ix<=x1;++ix)for(uint32_t ti:cells[(size_t)(iz*ny+iy)*nx+ix]){bool d=false;for(uint32_t o:out)if(o==ti){d=true;break;}if(!d)out.push_back(ti);}}

bool Octree::AABBInFrustum(const AABB&a,const Frustum&f){for(int i=0;i<6;++i){const Plane&p=f.planes[i];float px=(p.a>0)?a.max.x:a.min.x,py=(p.b>0)?a.max.y:a.min.y,pz=(p.c>0)?a.max.z:a.min.z;if(p.a*px+p.b*py+p.c*pz+p.d<0)return false;}return true;}
void Octree::Subdivide(OctreeNode*n){auto mn=n->bounds.min,mx=n->bounds.max;XMFLOAT3 mid={(mn.x+mx.x)*.5f,(mn.y+mx.y)*.5f,(mn.z+mx.z)*.5f};for(int i=0;i<8;++i){n->children[i]=std::make_unique<OctreeNode>();n->children[i]->isLeaf=true;n->children[i]->bounds.min={(i&1)?mid.x:mn.x,(i&2)?mid.y:mn.y,(i&4)?mid.z:mn.z};n->children[i]->bounds.max={(i&1)?mx.x:mid.x,(i&2)?mx.y:mid.y,(i&4)?mx.z:mid.z};}}
void Octree::BuildRecursive(OctreeNode*n,const std::vector<SceneObject>&o,const std::vector<int>&idx,int d,int mD,int mN){if((int)idx.size()<=mN||d>=mD){n->indices=idx;n->isLeaf=true;return;}n->isLeaf=false;Subdivide(n);XMFLOAT3 mid={(n->bounds.min.x+n->bounds.max.x)*.5f,(n->bounds.min.y+n->bounds.max.y)*.5f,(n->bounds.min.z+n->bounds.max.z)*.5f};std::vector<int>cb[8];for(int i:idx){auto&p=o[i].position;int c=((p.x>=mid.x)?1:0)|((p.y>=mid.y)?2:0)|((p.z>=mid.z)?4:0);cb[c].push_back(i);}for(int i=0;i<8;++i)BuildRecursive(n->children[i].get(),o,cb[i],d+1,mD,mN);}
void Octree::Build(const std::vector<SceneObject>&o,const std::vector<int>&si,int mD,int mN){root_.reset();if(si.empty())return;AABB b;b.min=o[si[0]].worldAABB.min;b.max=o[si[0]].worldAABB.max;for(int i:si){auto&obj=o[i];b.min.x=fminf(b.min.x,obj.worldAABB.min.x);b.min.y=fminf(b.min.y,obj.worldAABB.min.y);b.min.z=fminf(b.min.z,obj.worldAABB.min.z);b.max.x=fmaxf(b.max.x,obj.worldAABB.max.x);b.max.y=fmaxf(b.max.y,obj.worldAABB.max.y);b.max.z=fmaxf(b.max.z,obj.worldAABB.max.z);}root_=std::make_unique<OctreeNode>();root_->bounds=b;root_->isLeaf=true;BuildRecursive(root_.get(),o,si,0,mD,mN);}
void Octree::QueryNode(const OctreeNode*n,const Frustum&f,std::vector<int>&out)const{if(!AABBInFrustum(n->bounds,f))return;for(int i:n->indices)out.push_back(i);if(!n->isLeaf)for(int i=0;i<8;++i)QueryNode(n->children[i].get(),f,out);}
void Octree::QueryFrustum(const Frustum&f,std::vector<int>&out)const{if(root_)QueryNode(root_.get(),f,out);}

static XMVECTOR ClosestPointOnTriangle(XMVECTOR P,XMVECTOR A,XMVECTOR B,XMVECTOR C){XMVECTOR AB=B-A,AC=C-A,AP=P-A;float d1=XMVectorGetX(XMVector3Dot(AB,AP)),d2=XMVectorGetX(XMVector3Dot(AC,AP));if(d1<=0&&d2<=0)return A;XMVECTOR BP=P-B;float d3=XMVectorGetX(XMVector3Dot(AB,BP)),d4=XMVectorGetX(XMVector3Dot(AC,BP));if(d3>=0&&d4<=d3)return B;XMVECTOR CP=P-C;float d5=XMVectorGetX(XMVector3Dot(AB,CP)),d6=XMVectorGetX(XMVector3Dot(AC,CP));if(d6>=0&&d5<=d6)return C;float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){float v=d1/(d1-d3);return A+AB*v;}float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){float w=d2/(d2-d6);return A+AC*w;}float va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){float w=(d4-d3)/((d4-d3)+(d5-d6));return B+(C-B)*w;}float dn=1.0f/(va+vb+vc);return A+AB*(vb*dn)+AC*(vc*dn);}
static bool AABBInFrustumBrute(const AABB&a,const Frustum&f){for(int i=0;i<6;++i){const Plane&p=f.planes[i];float px=(p.a>0)?a.max.x:a.min.x,py=(p.b>0)?a.max.y:a.min.y,pz=(p.c>0)?a.max.z:a.min.z;if(p.a*px+p.b*py+p.c*pz+p.d<0)return false;}return true;}

Game::Game(HWND h,int w,int ht):hwnd_(h),width_(w),height_(ht){rs_=std::make_unique<RenderingSystem>(h,w,ht);}
void Game::RecalcAABB(SceneObject&o){o.worldAABB.min={o.position.x-o.scale,o.position.y-o.scale,o.position.z-o.scale};o.worldAABB.max={o.position.x+o.scale,o.position.y+o.scale,o.position.z+o.scale};}

bool Game::Initialize()
{
    if(!rs_->Initialize()) return false;
    wchar_t ep[MAX_PATH]={}; GetModuleFileNameW(nullptr,ep,MAX_PATH);
    std::wstring dir(ep); dir=dir.substr(0,dir.find_last_of(L"\\/")+1);
    const wchar_t* mc[]={L"sponza.obj",L"Sponza.obj",L"model.obj"};bool ml=false;
    for(auto*n:mc){std::wstring f=dir+n;if(GetFileAttributesW(f.c_str())!=INVALID_FILE_ATTRIBUTES){rs_->LoadMeshFromObj(f);ObjResult o=LoadObj(f);if(o.valid){triSoup_.reserve(o.indices.size());for(UINT i:o.indices)triSoup_.push_back(o.vertices[i].Position);}ml=true;break;}}
    if(!ml){rs_->LoadMeshFromBuiltin();float s=.5f;struct F{XMFLOAT3 v[4];};F faces[]={{{{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}}},{{{s,-s,-s},{-s,-s,-s},{-s,s,-s},{s,s,-s}}},{{{-s,-s,-s},{-s,-s,s},{-s,s,s},{-s,s,-s}}},{{{s,-s,s},{s,-s,-s},{s,s,-s},{s,s,s}}},{{{-s,s,s},{s,s,s},{s,s,-s},{-s,s,-s}}},{{{-s,-s,-s},{s,-s,-s},{s,-s,s},{-s,-s,s}}}};for(auto&f:faces){triSoup_.push_back(f.v[0]);triSoup_.push_back(f.v[1]);triSoup_.push_back(f.v[2]);triSoup_.push_back(f.v[0]);triSoup_.push_back(f.v[2]);triSoup_.push_back(f.v[3]);}}
    collGrid_.Build(triSoup_);
    auto lt=[&](const wchar_t*p,const wchar_t*j,int s,UINT sz=256,UINT t=32){TextureData td=LoadTextureWIC(dir+p);if(!td.valid)td=LoadTextureWIC(dir+j);if(!td.valid)td=CreateCheckerboard(sz,t);rs_->LoadTexture(td,s);};
    lt(L"texture1.png",L"texture1.jpg",0);lt(L"texture2.png",L"texture2.jpg",1,256,16);
    {TextureData td=LoadTextureWIC(dir+L"displacement.png");if(!td.valid)td=LoadTextureWIC(dir+L"displacement.jpg");if(!td.valid){const UINT sz=256;td.width=sz;td.height=sz;td.valid=true;td.pixels.resize(sz*sz*4);for(UINT y=0;y<sz;++y)for(UINT x=0;x<sz;++x){float h=.5f+.5f*sinf((float)x/sz*3.14159f*8)*cosf((float)y/sz*3.14159f*8);UINT i=(y*sz+x)*4;uint8_t v=(uint8_t)(h*255);td.pixels[i]=v;td.pixels[i+1]=v;td.pixels[i+2]=v;td.pixels[i+3]=255;}}rs_->LoadTexture(td,2);}
    {TextureData td=LoadTextureWIC(dir+L"normal.png");if(!td.valid)td=LoadTextureWIC(dir+L"normal.jpg");if(!td.valid){const UINT sz=256;td.width=sz;td.height=sz;td.valid=true;td.pixels.resize(sz*sz*4,0);for(UINT i=0;i<sz*sz;++i){td.pixels[i*4]=128;td.pixels[i*4+1]=128;td.pixels[i*4+2]=255;td.pixels[i*4+3]=255;}}rs_->LoadTexture(td,3);}

    srand(1337);auto rng=[](float lo,float hi)->float{return lo+(hi-lo)*((float)rand()/(float)RAND_MAX);};const float PI=3.14159265f;
    sceneObjects_.reserve(1000);
    for(int i=0;i<950;++i){SceneObject o={};o.position={rng(5,45),rng(.3f,14),rng(-7,7)};o.scale=rng(.2f,.5f);o.isStatic=true;o.basePosition=o.position;RecalcAABB(o);sceneObjects_.push_back(o);}
    for(int i=0;i<50;++i){SceneObject o={};o.basePosition={rng(10,40),rng(2,12),rng(-5,5)};o.position=o.basePosition;o.scale=rng(.8f,1.5f);o.isStatic=false;o.orbitRadius=rng(3,8);o.orbitSpeed=rng(.5f,2);o.orbitPhase=rng(0,2*PI);o.bobSpeed=rng(1,3);o.bobAmplitude=rng(1,3);RecalcAABB(o);sceneObjects_.push_back(o);}
    for(int i=0;i<(int)sceneObjects_.size();++i)(sceneObjects_[i].isStatic?staticIndices_:dynamicIndices_).push_back(i);
    octree_.Build(sceneObjects_,staticIndices_);octreeRebuilds_=1;
    SetupLights();return true;
}

void Game::SetupLights(){lights_.clear();
    {LightData l={};l.Type=(int)LightType::Point;l.Position={-8,3,0,1};l.Color={1,.8f,.6f,8};l.Range=50;lights_.push_back(l);}
    {LightData l={};l.Type=(int)LightType::Directional;l.Direction={.5f,-1,.5f,0};l.Color={1,1,1,1.5f};lights_.push_back(l);}
    {LightData l={};l.Type=(int)LightType::Spot;l.Position={8,8,0,1};l.Direction={0,-1,0,0};l.Color={.8f,.9f,1,10};l.Range=50;l.InnerConeAngle=XMConvertToRadians(20);l.OuterConeAngle=XMConvertToRadians(50);lights_.push_back(l);}
    // BRIGHT RED LIGHT at particle tornado center
    {LightData l={};l.Type=(int)LightType::Point;l.Position={0,5,0,1};l.Color={1.0f,0.1f,0.05f,40};l.Range=60;lights_.push_back(l);}
}

void Game::Shoot(){float cp=cosf(camPitch_);XMFLOAT3 d={sinf(camYaw_)*cp,-sinf(camPitch_),cosf(camYaw_)*cp};Projectile p;p.position={camX_,camY_,camZ_};p.velocity={d.x*projectileSpeed_,d.y*projectileSpeed_,d.z*projectileSpeed_};p.distTraveled=0;p.stuck=false;projectiles_.push_back(p);}

void Game::UpdateTitle(){wchar_t b[512];swprintf_s(b,L"Frame:%d | OctreeBuilds:%d | Static:%d/%d | Dynamic:%d/%d | DynCull:%d | PARTICLES:GPU-TORNADO | [G]FC [H]Octree",frameNumber_,octreeRebuilds_,visibleStatic_,(int)staticIndices_.size(),visibleDynamic_,(int)dynamicIndices_.size(),dynamicCullCalls_);SetWindowTextW(hwnd_,b);}

Frustum Game::BuildFrustum()const{XMVECTOR eye=XMVectorSet(camX_,camY_,camZ_,1);float cp=cosf(camPitch_);XMVECTOR fwd=XMVectorSet(sinf(camYaw_)*cp,-sinf(camPitch_),cosf(camYaw_)*cp,0);XMMATRIX view=XMMatrixLookAtLH(eye,eye+fwd,XMVectorSet(0,1,0,0));float asp=(rs_->GetHeight()>0)?(float)rs_->GetWidth()/rs_->GetHeight():1;XMMATRIX proj=XMMatrixPerspectiveFovLH(XMConvertToRadians(60),asp,nearPlane_,farPlane_);XMMATRIX vp=view*proj;XMFLOAT4X4 m;XMStoreFloat4x4(&m,vp);Frustum f;f.planes[0]={m._11+m._14,m._21+m._24,m._31+m._34,m._41+m._44};f.planes[1]={m._14-m._11,m._24-m._21,m._34-m._31,m._44-m._41};f.planes[2]={m._12+m._14,m._22+m._24,m._32+m._34,m._42+m._44};f.planes[3]={m._14-m._12,m._24-m._22,m._34-m._32,m._44-m._42};f.planes[4]={m._13,m._23,m._33,m._43};f.planes[5]={m._14-m._13,m._24-m._23,m._34-m._33,m._44-m._43};return f;}

void Game::UpdateDynamicObjects(float dt){for(int i:dynamicIndices_){auto&o=sceneObjects_[i];o.orbitPhase+=o.orbitSpeed*dt;o.position.x=o.basePosition.x+cosf(o.orbitPhase)*o.orbitRadius;o.position.z=o.basePosition.z+sinf(o.orbitPhase)*o.orbitRadius;o.position.y=o.basePosition.y+sinf(o.orbitPhase*o.bobSpeed)*o.bobAmplitude;RecalcAABB(o);}}

void Game::BuildParticleCB(ParticleCBData& cb, float dt, uint32_t emitCount) const
{
    XMVECTOR eye=XMVectorSet(camX_,camY_,camZ_,1);float cp=cosf(camPitch_);
    XMVECTOR fwd=XMVectorSet(sinf(camYaw_)*cp,-sinf(camPitch_),cosf(camYaw_)*cp,0);
    XMMATRIX view=XMMatrixLookAtLH(eye,eye+fwd,XMVectorSet(0,1,0,0));
    float asp=(rs_->GetHeight()>0)?(float)rs_->GetWidth()/rs_->GetHeight():1;
    XMMATRIX proj=XMMatrixPerspectiveFovLH(XMConvertToRadians(60),asp,nearPlane_,farPlane_);
    XMMATRIX inv=XMMatrixInverse(nullptr,view); XMFLOAT4X4 iv; XMStoreFloat4x4(&iv,inv);

    cb={};
    cb.DeltaTime    = dt;

    // BIG FOUNTAIN: low gravity + high upward speed = particles fly far and high
    cb.Gravity      = 6.0f;             // weaker gravity → particles stay up longer
    cb.MaxParticles = MAX_GPU_PARTICLES;
    cb.EmitCount    = emitCount;
    cb.EmitPosition = particleEmitPos_;

    // Long lifetime so particles arc through a big trajectory
    cb.LifeTime     = 6.0f;

    // EmitVelMin = (horizSpread, upMin, unused)
    // EmitVelMax = (unused, upMax, unused)
    // Big horizontal spread (12) + strong up speed (18..28) → wide tall arcs
    cb.EmitVelMin   = { 12.0f, 18.0f, 0.0f };
    cb.EmitVelMax   = {  0.0f, 28.0f, 0.0f };

    cb.Time         = time_;
    cb.View         = XMMatrixTranspose(view);
    cb.Proj         = XMMatrixTranspose(proj);

    // Big bright billboards
    cb.CamPosAndSize= { camX_, camY_, camZ_, 1.5f };
    // ROW extraction from inverse view matrix:
    //   Row 0 of inv(View) = world-space Right vector of camera
    //   Row 1 of inv(View) = world-space Up    vector of camera
    cb.CamRight     = { iv._11, iv._12, iv._13, 0 };
    cb.CamUp        = { iv._21, iv._22, iv._23, 0 };

    // Pump up brightness — pColor.w multiplies output color in shader
    cb.ParticleColor= { 1.0f, 1.0f, 1.0f, 2.5f };
}

void Game::Update(float dt, InputDevice* input)
{
    if(dt>.033f)dt=.033f; time_+=dt;
    uvOffsetX_+=.03f*dt;uvOffsetY_+=.01f*dt;if(uvOffsetX_>1)uvOffsetX_-=1;if(uvOffsetY_>1)uvOffsetY_-=1;
    if(input){
        if(input->IsMouseDown(1)){camYaw_+=input->GetMouseDeltaX()*mouseSensitivity_;camPitch_+=input->GetMouseDeltaY()*mouseSensitivity_;float lim=XMConvertToRadians(89);camPitch_=max(-lim,min(camPitch_,lim));}
        float cp=cosf(camPitch_);float fx=sinf(camYaw_)*cp,fy=-sinf(camPitch_),fz=cosf(camYaw_)*cp;float rx=cosf(camYaw_),rz=-sinf(camYaw_);float spd=moveSpeed_*dt;if(input->IsKeyDown(VK_SHIFT))spd*=3;
        if(input->IsKeyDown('W')){camX_+=fx*spd;camY_+=fy*spd;camZ_+=fz*spd;}if(input->IsKeyDown('S')){camX_-=fx*spd;camY_-=fy*spd;camZ_-=fz*spd;}
        if(input->IsKeyDown('D')){camX_+=rx*spd;camZ_+=rz*spd;}if(input->IsKeyDown('A')){camX_-=rx*spd;camZ_-=rz*spd;}
        if(input->IsKeyDown('E'))camY_+=spd;if(input->IsKeyDown('Q'))camY_-=spd;
        bool sp=input->IsKeyDown(VK_SPACE);if(sp&&!prevSpace_)Shoot();prevSpace_=sp;
        bool f1=input->IsKeyDown('F');if(f1&&!prevF1_){wireframe_=!wireframe_;rs_->SetWireframe(wireframe_);}prevF1_=f1;
        bool g=input->IsKeyDown('G');if(g&&!prevG_)frustumCulling_=!frustumCulling_;prevG_=g;
        bool h=input->IsKeyDown('H');if(h&&!prevH_)useOctree_=!useOctree_;prevH_=h;
    }
    UpdateDynamicObjects(dt);
    const float r=projectileRadius_,r2=r*r,sub=r*.5f;
    for(auto&p:projectiles_){if(p.stuck)continue;float md=projectileSpeed_*dt;p.distTraveled+=md;if(p.distTraveled>=projectileMaxDist_){p.stuck=true;continue;}float is=1.0f/projectileSpeed_;XMFLOAT3 df={p.velocity.x*is,p.velocity.y*is,p.velocity.z*is};XMVECTOR dir=XMLoadFloat3(&df);float rem=md;while(rem>0){float step=min(rem,sub);rem-=step;XMVECTOR pos=XMLoadFloat3(&p.position);pos+=dir*step;XMStoreFloat3(&p.position,pos);if(collGrid_.Empty())continue;queryBuf_.clear();collGrid_.Query(p.position,r,queryBuf_);bool hit=false;for(uint32_t ti:queryBuf_){XMVECTOR A=XMLoadFloat3(&triSoup_[ti*3]),B=XMLoadFloat3(&triSoup_[ti*3+1]),C=XMLoadFloat3(&triSoup_[ti*3+2]);XMVECTOR cl=ClosestPointOnTriangle(pos,A,B,C),diff=pos-cl;float d2=XMVectorGetX(XMVector3Dot(diff,diff));if(d2<=r2){if(d2>1e-8f){float d=sqrtf(d2);pos=cl+diff*(r/d);XMStoreFloat3(&p.position,pos);}p.stuck=true;hit=true;break;}}if(hit)break;}}
}

void Game::BuildGeometryCB(GeometryCBData&cb,TessImportance imp)const{XMVECTOR eye=XMVectorSet(camX_,camY_,camZ_,1);float cp=cosf(camPitch_);XMVECTOR fwd=XMVectorSet(sinf(camYaw_)*cp,-sinf(camPitch_),cosf(camYaw_)*cp,0);XMMATRIX view=XMMatrixLookAtLH(eye,eye+fwd,XMVectorSet(0,1,0,0));float asp=(rs_->GetHeight()>0)?(float)rs_->GetWidth()/rs_->GetHeight():1;XMMATRIX proj=XMMatrixPerspectiveFovLH(XMConvertToRadians(60),asp,nearPlane_,farPlane_);float tMin=1,tMax=1;TessImportanceToRange(imp,tMin,tMax);cb={};cb.World=XMMatrixTranspose(XMMatrixIdentity());cb.View=XMMatrixTranspose(view);cb.Proj=XMMatrixTranspose(proj);cb.Tiling={4,4};cb.UVOffset={uvOffsetX_,uvOffsetY_};cb.CameraPos={camX_,camY_,camZ_,1};cb.DisplacementScale=1;cb.TessMin=tMin;cb.TessMax=tMax;}
void Game::BuildGeometryCBForObject(GeometryCBData&o,const SceneObject&obj)const{BuildGeometryCB(o,TessImportance::None);o.World=XMMatrixTranspose(XMMatrixScaling(obj.scale,obj.scale,obj.scale)*XMMatrixTranslation(obj.position.x,obj.position.y,obj.position.z));o.DisplacementScale=0;if(!obj.isStatic)o.Tiling={1,1};}
void Game::BuildGeometryCBForSphere(GeometryCBData&o,XMFLOAT3 p,float s)const{BuildGeometryCB(o,TessImportance::None);o.World=XMMatrixTranspose(XMMatrixScaling(s,s,s)*XMMatrixTranslation(p.x,p.y,p.z));o.DisplacementScale=0;}

void Game::BuildLightingCB(LightingCBData&cb)const{cb={};cb.CameraPos={camX_,camY_,camZ_,1};std::vector<LightData>all=lights_;for(int i:dynamicIndices_){if((int)all.size()>=MAX_LIGHTS)break;auto&o=sceneObjects_[i];LightData l={};l.Type=(int)LightType::Point;l.Position={o.position.x,o.position.y,o.position.z,1};l.Color={1,.3f,.1f,15};l.Range=12;all.push_back(l);}auto addPL=[&](const Projectile&p){if((int)all.size()>=MAX_LIGHTS)return;LightData l={};l.Type=(int)LightType::Point;l.Position={p.position.x,p.position.y,p.position.z,1};l.Color={.7f,0,1,20};l.Range=30;all.push_back(l);};for(auto&p:projectiles_)if(p.stuck)addPL(p);for(auto&p:projectiles_)if(!p.stuck)addPL(p);cb.LightCount=(int)all.size();int cnt=min((int)all.size(),MAX_LIGHTS);for(int i=0;i<cnt;++i)cb.Lights[i]=all[i];
    // Shadow / cascade fields are filled separately in Render() after ComputeCascades()
    cb.CascadeCount=CASCADE_COUNT;
    cb.ShadowMapSize=(float)SHADOW_MAP_SIZE;
    cb.ShadowBias=0.0008f;   // scaled by cascadeScale (1,2,4,8) in shader -> 0.0008..0.0064
}

// ---------------- Cascaded Shadow Maps helpers ----------------

void Game::ComputeCascades(CascadeData& out) const
{
    XMFLOAT3 dir = { 0.5f, -1.0f, 0.5f };
    for(const auto& l : lights_) if(l.Type==(int)LightType::Directional){ dir={l.Direction.x,l.Direction.y,l.Direction.z}; break; }
    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&dir));
    out.lightDir = dir;

    XMVECTOR eye = XMVectorSet(camX_, camY_, camZ_, 1);
    float cp = cosf(camPitch_);
    XMVECTOR fwd = XMVectorSet(sinf(camYaw_)*cp, -sinf(camPitch_), cosf(camYaw_)*cp, 0);
    XMMATRIX view = XMMatrixLookAtLH(eye, eye+fwd, XMVectorSet(0,1,0,0));
    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    float aspect = (rs_->GetHeight()>0) ? (float)rs_->GetWidth()/rs_->GetHeight() : 1.0f;
    float fov = XMConvertToRadians(60.0f);
    float tanHalfFov = tanf(fov * 0.5f);

    // Practical split scheme — log/uniform mix
    float n = nearPlane_;
    float f = 200.0f;
    float lambda = 0.7f;
    float splits[CASCADE_COUNT+1];
    splits[0] = n;
    for(int i=1;i<=CASCADE_COUNT;++i){
        float si = (float)i / (float)CASCADE_COUNT;
        float logSplit = n * powf(f/n, si);
        float uniSplit = n + (f - n) * si;
        splits[i] = lambda*logSplit + (1.0f - lambda)*uniSplit;
    }

    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    for(int c = 0; c < CASCADE_COUNT; ++c){
        float nC = splits[c];
        float fC = splits[c+1];

        // Frustum slice corners in CAMERA space
        float nearH = tanHalfFov * nC, nearW = nearH * aspect;
        float farH  = tanHalfFov * fC, farW  = farH  * aspect;
        XMVECTOR cornersCS[8] = {
            XMVectorSet(-nearW, -nearH, nC, 1), XMVectorSet( nearW, -nearH, nC, 1),
            XMVectorSet(-nearW,  nearH, nC, 1), XMVectorSet( nearW,  nearH, nC, 1),
            XMVectorSet(-farW,  -farH,  fC, 1), XMVectorSet( farW,  -farH,  fC, 1),
            XMVectorSet(-farW,   farH,  fC, 1), XMVectorSet( farW,   farH,  fC, 1),
        };

        // World-space corners + bounding sphere center
        XMVECTOR cornersWS[8];
        XMVECTOR centerWS = XMVectorZero();
        for(int i=0;i<8;++i){ cornersWS[i] = XMVector3TransformCoord(cornersCS[i], invView); centerWS = XMVectorAdd(centerWS, cornersWS[i]); }
        centerWS = XMVectorScale(centerWS, 1.0f/8.0f);

        // Bounding sphere radius (rotation-invariant)
        float radius = 0.0f;
        for(int i=0;i<8;++i){
            float d = XMVectorGetX(XMVector3Length(XMVectorSubtract(cornersWS[i], centerWS)));
            if(d > radius) radius = d;
        }
        radius = ceilf(radius * 4.0f) / 4.0f;  // light quantization

        // Light view: looking AT the cascade center FROM behind it along light direction.
        // Pull-back distance is large enough to capture tall casters (Sponza walls).
        const float pullBack = 250.0f;
        XMVECTOR lightPos = XMVectorSubtract(centerWS, XMVectorScale(L, pullBack));
        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, centerWS, up);

        // Orthographic projection covering the bounding sphere; depth range covers
        // pull-back distance (for casters above) + sphere diameter.
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
            -radius, radius, -radius, radius,
            0.0f, pullBack + 2.0f * radius);

        out.lightVP[c]    = XMMatrixMultiply(lightView, lightProj);
        out.splitFar[c]   = fC;
        XMStoreFloat3(&out.sphereCenter[c], centerWS);
        out.sphereRadius[c] = radius;
    }
}

void Game::BuildShadowCBForScene(ShadowCBData& cb, const XMMATRIX& lightVP) const
{
    cb = {};
    cb.World   = XMMatrixTranspose(XMMatrixIdentity());
    cb.LightVP = XMMatrixTranspose(lightVP);
}

void Game::BuildShadowCBForObject(ShadowCBData& cb, const SceneObject& obj, const XMMATRIX& lightVP) const
{
    cb = {};
    XMMATRIX world = XMMatrixScaling(obj.scale,obj.scale,obj.scale) *
                     XMMatrixTranslation(obj.position.x,obj.position.y,obj.position.z);
    cb.World   = XMMatrixTranspose(world);
    cb.LightVP = XMMatrixTranspose(lightVP);
}

void Game::BuildShadowCBForSphere(ShadowCBData& cb, XMFLOAT3 p, float s, const XMMATRIX& lightVP) const
{
    cb = {};
    XMMATRIX world = XMMatrixScaling(s,s,s) * XMMatrixTranslation(p.x,p.y,p.z);
    cb.World   = XMMatrixTranspose(world);
    cb.LightVP = XMMatrixTranspose(lightVP);
}

void Game::Render()
{
    ++frameNumber_;
    LightingCBData lCB; BuildLightingCB(lCB);

    // Compute cascades + fill shadow-related fields in lighting CB
    CascadeData cascades; ComputeCascades(cascades);
    for(int c=0;c<CASCADE_COUNT;++c) lCB.LightVP[c] = XMMatrixTranspose(cascades.lightVP[c]);
    lCB.CascadeSplits = { cascades.splitFar[0], cascades.splitFar[1], cascades.splitFar[2], cascades.splitFar[3] };
    lCB.LightDir      = { cascades.lightDir.x, cascades.lightDir.y, cascades.lightDir.z, 0.0f };

    Frustum frustum=BuildFrustum();
    std::vector<int>visStat,visDyn;visStat.reserve(staticIndices_.size());visDyn.reserve(dynamicIndices_.size());
    if(!frustumCulling_){visStat=staticIndices_;visDyn=dynamicIndices_;}else{
        if(useOctree_)octree_.QueryFrustum(frustum,visStat);else for(int i:staticIndices_)if(AABBInFrustumBrute(sceneObjects_[i].worldAABB,frustum))visStat.push_back(i);
        for(int i:dynamicIndices_)if(AABBInFrustumBrute(sceneObjects_[i].worldAABB,frustum))visDyn.push_back(i);++dynamicCullCalls_;}
    visibleStatic_=(int)visStat.size();visibleDynamic_=(int)visDyn.size();UpdateTitle();

    rs_->BeginFrame();

    // Build particle CB ONCE — reuse for Dispatch and Draw
    ParticleCBData pcb; BuildParticleCB(pcb, 1.0f/60.0f, particleEmitRate_);

    // COMPUTE: update + emit particles on GPU
    rs_->DispatchParticles(pcb);

    // ---------------------------------------------------------------
    // SHADOW PASS — cascaded, with aggressive per-cascade culling
    // ---------------------------------------------------------------
    {
        XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&cascades.lightDir));
        const float LIGHT_PULLBACK = 50.0f;

        XMFLOAT3 cExtCenter[CASCADE_COUNT];
        float    cEffRadius[CASCADE_COUNT];
        for(int c=0;c<CASCADE_COUNT;++c){
            XMVECTOR center = XMLoadFloat3(&cascades.sphereCenter[c]);
            XMVECTOR ext    = XMVectorSubtract(center, XMVectorScale(L, LIGHT_PULLBACK*0.5f));
            XMStoreFloat3(&cExtCenter[c], ext);
            cEffRadius[c]   = cascades.sphereRadius[c] + LIGHT_PULLBACK*0.5f;
        }

        for(int c = 0; c < CASCADE_COUNT; ++c){
            rs_->BeginShadowPass(c);

            // Sponza always — single big static caster
            { ShadowCBData scb; BuildShadowCBForScene(scb, cascades.lightVP[c]); rs_->DrawShadowMesh(scb); }

            // Outermost cascade: skip ALL sphere objects entirely.
            // Their shadows would be ~1 texel and barely visible from such a wide cascade.
            if(c == CASCADE_COUNT - 1){ rs_->EndShadowPass(); continue; }

            const float effR = cEffRadius[c];
            const XMFLOAT3 ec = cExtCenter[c];

            // Stricter sub-pixel threshold: object must be at least 8 texels wide in shadow map.
            // For a cascade of radius R: object_world_size / (2R) * SHADOW_MAP_SIZE > 8
            // => scale > 8 * R / (SHADOW_MAP_SIZE / 2) = 16*R/SHADOW_MAP_SIZE
            const float minObjScaleForCascade = (cascades.sphereRadius[c] * 16.0f) / (float)SHADOW_MAP_SIZE;

            for(int i : staticIndices_){
                const auto& o = sceneObjects_[i];
                if(o.scale < minObjScaleForCascade) continue;
                float dx=o.position.x-ec.x, dy=o.position.y-ec.y, dz=o.position.z-ec.z;
                float threshold = effR + o.scale;
                if(dx*dx+dy*dy+dz*dz > threshold*threshold) continue;
                ShadowCBData scb; BuildShadowCBForObject(scb, o, cascades.lightVP[c]);
                rs_->DrawShadowSphere(scb);
            }
            for(int i : dynamicIndices_){
                const auto& o = sceneObjects_[i];
                if(o.scale < minObjScaleForCascade) continue;
                float dx=o.position.x-ec.x, dy=o.position.y-ec.y, dz=o.position.z-ec.z;
                float threshold = effR + o.scale;
                if(dx*dx+dy*dy+dz*dz > threshold*threshold) continue;
                ShadowCBData scb; BuildShadowCBForObject(scb, o, cascades.lightVP[c]);
                rs_->DrawShadowSphere(scb);
            }

            rs_->EndShadowPass();
        }
    }

    rs_->BeginGeometryPass();
    {GeometryCBData cb;BuildGeometryCB(cb,TessImportance::High);rs_->DrawSceneMeshTess(cb);}
    for(int i:visStat){GeometryCBData cb;BuildGeometryCBForObject(cb,sceneObjects_[i]);rs_->DrawSphere(cb);}
    for(int i:visDyn){GeometryCBData cb;BuildGeometryCBForObject(cb,sceneObjects_[i]);rs_->DrawSphere(cb);}
    for(auto&p:projectiles_){GeometryCBData cb;BuildGeometryCBForSphere(cb,p.position,projectileRadius_);rs_->DrawSphere(cb);}

    // RENDER: draw particle billboards into GBuffer
    rs_->DrawParticles(pcb);

    rs_->EndGeometryPass();
    rs_->DoLightingPass(lCB);
    rs_->EndFrame();
}

void Game::Resize(int w,int h){width_=w;height_=h;rs_->Resize(w,h);}
