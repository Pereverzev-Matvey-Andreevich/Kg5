struct LightData{float4 Position;float4 Direction;float4 Color;int Type;float Range;float InnerConeAngle;float OuterConeAngle;};
cbuffer GeometryCB:register(b0){float4x4 gWorld;float4x4 gView;float4x4 gProj;float2 gTiling;float2 gUVOffset;float4 gCameraPos;float gDisplacementScale;float gTessMin;float gTessMax;float _Pad0;float4 _Pad1;};
Texture2D gTexture:register(t0);Texture2D gTexture2:register(t1);Texture2D gDisplacementMap:register(t2);Texture2D gNormalMap:register(t3);SamplerState gSampler:register(s0);
struct GeomVSIn{float3 Position:POSITION;float3 Normal:NORMAL;float4 Color:COLOR;float2 TexCoord:TEXCOORD;};
struct GeomPSIn{float4 ClipPos:SV_POSITION;float3 WorldPos:TEXCOORD0;float3 Normal:TEXCOORD1;float4 Color:COLOR;float2 TexCoord:TEXCOORD2;float2 RawUV:TEXCOORD3;};
struct GeomPSOut{float4 Albedo:SV_TARGET0;float4 Normal:SV_TARGET1;float4 WorldPos:SV_TARGET2;};
GeomPSIn GeometryVS(GeomVSIn i){GeomPSIn o;float4 w=mul(float4(i.Position,1),gWorld);o.ClipPos=mul(mul(w,gView),gProj);o.WorldPos=w.xyz;o.Normal=normalize(mul(float4(i.Normal,0),gWorld).xyz);o.Color=i.Color;o.TexCoord=i.TexCoord*gTiling+gUVOffset;o.RawUV=i.TexCoord;return o;}
GeomPSOut GeometryPS(GeomPSIn i){float4 tc=gTexture.Sample(gSampler,i.TexCoord);float3 N=normalize(i.Normal);float3 dp1=ddx(i.WorldPos);float3 dp2=ddy(i.WorldPos);float2 duv1=ddx(i.TexCoord);float2 duv2=ddy(i.TexCoord);float3 dp2p=cross(dp2,N);float3 dp1p=cross(N,dp1);float3 T=dp2p*duv1.x+dp1p*duv2.x;float3 B=dp2p*duv1.y+dp1p*duv2.y;float inv=rsqrt(max(dot(T,T),dot(B,B))+1e-8f);float3x3 TBN=float3x3(T*inv,B*inv,N);float3 nTS=gNormalMap.Sample(gSampler,i.TexCoord).xyz*2-1;N=normalize(mul(nTS,TBN));GeomPSOut o;o.Albedo=float4(tc.rgb,1);o.Normal=float4(N*.5+.5,1);o.WorldPos=float4(i.WorldPos,1);return o;}
struct TessVSOut{float4 PosW:SV_POSITION;float3 Normal:TEXCOORD1;float4 Color:COLOR;float2 TexCoord:TEXCOORD2;float2 RawUV:TEXCOORD3;};
TessVSOut TessVS(GeomVSIn i){TessVSOut o;float4 w=mul(float4(i.Position,1),gWorld);o.PosW=w;o.Normal=normalize(mul(float4(i.Normal,0),gWorld).xyz);o.Color=i.Color;o.TexCoord=i.TexCoord*gTiling+gUVOffset;o.RawUV=i.TexCoord;return o;}
struct PatchTess{float EdgeTess[3]:SV_TessFactor;float InsideTess:SV_InsideTessFactor;};
float ComputeTessFactor(float3 wp){float d=distance(wp,gCameraPos.xyz);float t=saturate((d-2)/28);return lerp(gTessMax,gTessMin,t);}
PatchTess PatchHS(InputPatch<TessVSOut,3> p,uint pid:SV_PrimitiveID){PatchTess pt;float3 m01=(p[0].PosW.xyz+p[1].PosW.xyz)*.5;float3 m12=(p[1].PosW.xyz+p[2].PosW.xyz)*.5;float3 m20=(p[2].PosW.xyz+p[0].PosW.xyz)*.5;float3 c=(p[0].PosW.xyz+p[1].PosW.xyz+p[2].PosW.xyz)/3;pt.EdgeTess[0]=ComputeTessFactor(m12);pt.EdgeTess[1]=ComputeTessFactor(m20);pt.EdgeTess[2]=ComputeTessFactor(m01);pt.InsideTess=ComputeTessFactor(c);return pt;}
[domain("tri")][partitioning("fractional_odd")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("PatchHS")][maxtessfactor(64.0f)]TessVSOut TessHS(InputPatch<TessVSOut,3> p,uint i:SV_OutputControlPointID,uint pid:SV_PrimitiveID){return p[i];}
[domain("tri")]GeomPSIn TessDS(PatchTess pt,float3 b:SV_DomainLocation,const OutputPatch<TessVSOut,3> p){float4 posW=b.x*p[0].PosW+b.y*p[1].PosW+b.z*p[2].PosW;float3 n=b.x*p[0].Normal+b.y*p[1].Normal+b.z*p[2].Normal;float2 tc=b.x*p[0].TexCoord+b.y*p[1].TexCoord+b.z*p[2].TexCoord;float2 ruv=b.x*p[0].RawUV+b.y*p[1].RawUV+b.z*p[2].RawUV;float4 col=b.x*p[0].Color+b.y*p[1].Color+b.z*p[2].Color;n=normalize(n);float dist=distance(posW.xyz,gCameraPos.xyz);float mip=clamp(log2(max(1,dist/8)),0,6);float h=gDisplacementMap.SampleLevel(gSampler,tc,mip).r;posW.xyz+=n*(h*gDisplacementScale);GeomPSIn o;o.ClipPos=mul(mul(posW,gView),gProj);o.WorldPos=posW.xyz;o.Normal=n;o.Color=col;o.TexCoord=tc;o.RawUV=ruv;return o;}

// ============================================================================
// SHADOW PASS — depth-only, one cascade at a time
// ============================================================================
cbuffer ShadowCB : register(b0) {
    float4x4 gShadowWorld;
    float4x4 gShadowLightVP;
    float4   _ShadowPad[8];
};

float4 ShadowVS(GeomVSIn i) : SV_POSITION {
    float4 w = mul(float4(i.Position, 1), gShadowWorld);
    return mul(w, gShadowLightVP);
}

// ============================================================================
// LIGHTING PASS — extended with CSM + 3x3 PCF
// ============================================================================
#define CASCADE_COUNT 4

cbuffer LightingCB : register(b0) {
    float4   gCameraPos_L;
    int      gLightCount;
    int      gCascadeCount;
    float    gShadowMapSize;
    float    gShadowBias;
    LightData gLights[64];
    float4x4 gLightVP[CASCADE_COUNT];
    float4   gCascadeSplits;          // .x .y .z .w = far distance per cascade
    float4   gLightDir;               // direction of shadowed dir light
    float4   _LightCBPad[12];
};

Texture2D gAlbedo   : register(t0);
Texture2D gNormal   : register(t1);
Texture2D gWorldPos : register(t2);
Texture2DArray<float> gShadowMaps : register(t3);

SamplerState           gSamplerL      : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

void LightingVS(uint id:SV_VertexID, out float4 pos:SV_POSITION, out float2 uv:TEXCOORD0) {
    uv = float2((id<<1)&2, id&2);
    pos = float4(uv.x*2-1, 1-uv.y*2, 0, 1);
}

float3 CalcLight(LightData l, float3 alb, float3 N, float3 V, float3 wp) {
    float3 L = float3(0,0,0); float att = 1; float3 rad = l.Color.rgb * l.Color.w;
    if (l.Type == 1) { L = normalize(-l.Direction.xyz); att = 1; }
    else if (l.Type == 0) { float3 tl=l.Position.xyz-wp; float d=length(tl); if(d>l.Range) return float3(0,0,0); L=tl/d; float t=1-saturate(d/l.Range); att=t*t; }
    else if (l.Type == 2) { float3 tl=l.Position.xyz-wp; float d=length(tl); if(d>l.Range) return float3(0,0,0); L=tl/d; float t=1-saturate(d/l.Range); att=t*t; float ca=dot(-L,normalize(l.Direction.xyz)); float inner=cos(l.InnerConeAngle), outer=cos(l.OuterConeAngle); float cone=saturate((ca-outer)/max(inner-outer,.001)); att*=cone*cone; }
    if (att <= 0) return float3(0,0,0);
    float NdL = max(dot(N,L), 0); float3 diff = NdL * alb * rad * att;
    float3 H = normalize(L+V); float NdH = max(dot(N,H), 0); float3 spec = pow(NdH, 32) * .3 * rad * att;
    return diff + spec;
}

// Pick cascade based on world-space distance from camera (split scheme)
int SelectCascade(float3 wp) {
    float dist = distance(wp, gCameraPos_L.xyz);
    if (dist < gCascadeSplits.x) return 0;
    if (dist < gCascadeSplits.y) return 1;
    if (dist < gCascadeSplits.z) return 2;
    return 3;
}

// 3x3 PCF — returns visibility 0..1 (1 = fully lit)
float SampleShadowPCF(float3 wp, int cascade, float NdotL) {
    float4 lp = mul(float4(wp, 1), gLightVP[cascade]);
    float3 sc = lp.xyz / lp.w;

    // Clip-space [-1,1] -> texture UV [0,1] (Y flipped because of D3D top-left origin)
    float2 uv = sc.xy * float2(0.5, -0.5) + 0.5;

    // Outside cascade -> treat as fully lit
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1 || sc.z < 0 || sc.z > 1) return 1.0;

    // Bias must scale with cascade — outer cascades have larger texels in world space.
    // Cascade 0 = scale 1.0, 1 = 2.0, 2 = 4.0, 3 = 8.0
    float cascadeScale = (float)(1 << cascade);

    // Constant + slope-scaled bias (more bias on shallow normals)
    float baseBias  = gShadowBias * cascadeScale;
    float slopeBias = max(0.005 * (1.0 - NdotL), 0.001) * cascadeScale;
    float depth = sc.z - baseBias - slopeBias;

    // Larger PCF kernel for outer cascades
    float texelSize = (1.0 / gShadowMapSize) * (1.0 + (float)cascade * 0.5);

    float sum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 off = float2(x, y) * texelSize;
            sum += gShadowMaps.SampleCmpLevelZero(gShadowSampler, float3(uv + off, cascade), depth);
        }
    }
    return sum / 9.0;
}

float4 LightingPS(float4 pos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    float4 a4 = gAlbedo.Sample(gSamplerL, uv);
    float4 n4 = gNormal.Sample(gSamplerL, uv);
    float4 w4 = gWorldPos.Sample(gSamplerL, uv);

    if (w4.w < .01) return float4(.05, .08, .15, 1);
    if (a4.a < .5)  { float3 e = a4.rgb * 3; e = e/(e+1); e = pow(abs(e), 1/2.2); return float4(e, 1); }

    float3 alb = a4.rgb;
    float3 N   = normalize(n4.xyz * 2 - 1);
    float3 wp  = w4.xyz;
    float3 V   = normalize(gCameraPos_L.xyz - wp);

    // Shadow factor for the directional light (computed once per pixel)
    int   cascade = SelectCascade(wp);
    float NdotL   = max(dot(N, normalize(-gLightDir.xyz)), 0.0);
    float shadow  = SampleShadowPCF(wp, cascade, NdotL);

    float3 col = alb * .08;
    int cnt = clamp(gLightCount, 0, 64);
    [loop]
    for (int i = 0; i < cnt; ++i) {
        float3 contrib = CalcLight(gLights[i], alb, N, V, wp);
        if (gLights[i].Type == 1) contrib *= shadow;   // shadow only directional
        col += contrib;
    }

    col = col / (col + 1);
    col = pow(abs(col), 1/2.2);
    return float4(col, 1);
}
