struct LightData
{
    float4 Position;
    float4 Direction;
    float4 Color;
    int    Type;
    float  Range;
    float  InnerConeAngle;
    float  OuterConeAngle;
};

cbuffer GeometryCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float2   gTiling;
    float2   gUVOffset;
    float4   gCameraPos;
    float    gDisplacementScale;
    float3   _Pad0;
    float4   _Pad1;
};

Texture2D    gTexture         : register(t0);
Texture2D    gTexture2        : register(t1);
Texture2D    gDisplacementMap : register(t2);
Texture2D    gNormalMap       : register(t3);
SamplerState gSampler         : register(s0);

struct GeomVSIn
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct GeomPSIn
{
    float4 ClipPos  : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD2;
    float2 RawUV    : TEXCOORD3;
};

struct GeomPSOut
{
    float4 Albedo   : SV_TARGET0;
    float4 Normal   : SV_TARGET1;
    float4 WorldPos : SV_TARGET2;
};

GeomPSIn GeometryVS(GeomVSIn input)
{
    GeomPSIn o;
    float4 wPos = mul(float4(input.Position, 1.0f), gWorld);
    o.ClipPos   = mul(mul(wPos, gView), gProj);
    o.WorldPos  = wPos.xyz;
    o.Normal    = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    o.Color     = input.Color;
    o.TexCoord  = input.TexCoord * gTiling + gUVOffset;
    o.RawUV     = input.TexCoord;
    return o;
}

GeomPSOut GeometryPS(GeomPSIn input)
{
    float4 texColor = gTexture.Sample(gSampler, input.TexCoord);

    float3 N = normalize(input.Normal);

    float3 dp1  = ddx(input.WorldPos);
    float3 dp2  = ddy(input.WorldPos);
    float2 duv1 = ddx(input.TexCoord);
    float2 duv2 = ddy(input.TexCoord);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float  invmax = rsqrt(max(dot(T, T), dot(B, B)) + 1e-8f);
    float3x3 TBN = float3x3(T * invmax, B * invmax, N);

    float3 normalTS = gNormalMap.Sample(gSampler, input.TexCoord).xyz * 2.0f - 1.0f;
    N = normalize(mul(normalTS, TBN));

    GeomPSOut o;
    o.Albedo   = float4(texColor.rgb, 1.0f);
    o.Normal   = float4(N * 0.5f + 0.5f, 1.0f);
    o.WorldPos = float4(input.WorldPos, 1.0f);
    return o;
}

struct TessVSOut
{
    float4 PosW     : SV_POSITION;
    float3 Normal   : TEXCOORD1;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD2;
    float2 RawUV    : TEXCOORD3;
};

TessVSOut TessVS(GeomVSIn input)
{
    TessVSOut o;
    float4 wPos = mul(float4(input.Position, 1.0f), gWorld);
    o.PosW     = wPos;
    o.Normal   = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    o.Color    = input.Color;
    o.TexCoord = input.TexCoord * gTiling + gUVOffset;
    o.RawUV    = input.TexCoord;
    return o;
}

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess  : SV_InsideTessFactor;
};

float ComputeTessFactor(float3 worldPos)
{
    float dist    = distance(worldPos, gCameraPos.xyz);
    float minDist = 2.0f;
    float maxDist = 30.0f;
    float t = saturate((dist - minDist) / (maxDist - minDist));
    return lerp(24.0f, 1.0f, t);
}

PatchTess PatchHS(InputPatch<TessVSOut, 3> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;
    float3 mid01  = (patch[0].PosW.xyz + patch[1].PosW.xyz) * 0.5f;
    float3 mid12  = (patch[1].PosW.xyz + patch[2].PosW.xyz) * 0.5f;
    float3 mid20  = (patch[2].PosW.xyz + patch[0].PosW.xyz) * 0.5f;
    float3 center = (patch[0].PosW.xyz + patch[1].PosW.xyz + patch[2].PosW.xyz) / 3.0f;
    pt.EdgeTess[0] = ComputeTessFactor(mid12);
    pt.EdgeTess[1] = ComputeTessFactor(mid20);
    pt.EdgeTess[2] = ComputeTessFactor(mid01);
    pt.InsideTess  = ComputeTessFactor(center);
    return pt;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchHS")]
[maxtessfactor(64.0f)]
TessVSOut TessHS(InputPatch<TessVSOut, 3> patch,
                 uint i       : SV_OutputControlPointID,
                 uint patchID : SV_PrimitiveID)
{
    return patch[i];
}

[domain("tri")]
GeomPSIn TessDS(PatchTess pt,
                float3 bary : SV_DomainLocation,
                const OutputPatch<TessVSOut, 3> patch)
{
    float4 posW     = bary.x * patch[0].PosW     + bary.y * patch[1].PosW     + bary.z * patch[2].PosW;
    float3 normal   = bary.x * patch[0].Normal   + bary.y * patch[1].Normal   + bary.z * patch[2].Normal;
    float2 texCoord = bary.x * patch[0].TexCoord + bary.y * patch[1].TexCoord + bary.z * patch[2].TexCoord;
    float2 rawUV    = bary.x * patch[0].RawUV    + bary.y * patch[1].RawUV    + bary.z * patch[2].RawUV;
    float4 color    = bary.x * patch[0].Color    + bary.y * patch[1].Color    + bary.z * patch[2].Color;

    normal = normalize(normal);

    float dist  = distance(posW.xyz, gCameraPos.xyz);
    float mip   = clamp(log2(max(1.0f, dist / 8.0f)), 0.0f, 6.0f);
    float height = gDisplacementMap.SampleLevel(gSampler, texCoord, mip).r;
    posW.xyz += normal * (height * gDisplacementScale);

    GeomPSIn o;
    o.ClipPos  = mul(mul(posW, gView), gProj);
    o.WorldPos = posW.xyz;
    o.Normal   = normal;
    o.Color    = color;
    o.TexCoord = texCoord;
    o.RawUV    = rawUV;
    return o;
}

cbuffer LightingCB : register(b0)
{
    float4    gCameraPos_L;
    int       gLightCount;
    float3    _LightPad;
    LightData gLights[64];
    float4    _LightCBPad[14];
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gWorldPos : register(t2);
SamplerState gSamplerL : register(s0);

void LightingVS(uint id : SV_VertexID,
                out float4 pos : SV_POSITION,
                out float2 uv  : TEXCOORD0)
{
    uv  = float2((id << 1) & 2, id & 2);
    pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}

float3 CalcLight(LightData light, float3 albedo, float3 N, float3 V, float3 worldPos)
{
    float3 L           = float3(0, 0, 0);
    float  attenuation = 1.0f;
    float3 radiance    = light.Color.rgb * light.Color.w;

    if (light.Type == 1)
    {
        L           = normalize(-light.Direction.xyz);
        attenuation = 1.0f;
    }
    else if (light.Type == 0)
    {
        float3 toLight = light.Position.xyz - worldPos;
        float  dist    = length(toLight);
        if (dist > light.Range) return float3(0, 0, 0);
        L = toLight / dist;
        float t = 1.0f - saturate(dist / light.Range);
        attenuation = t * t;
    }
    else if (light.Type == 2)
    {
        float3 toLight = light.Position.xyz - worldPos;
        float  dist    = length(toLight);
        if (dist > light.Range) return float3(0, 0, 0);
        L = toLight / dist;
        float t = 1.0f - saturate(dist / light.Range);
        attenuation = t * t;
        float cosAngle = dot(-L, normalize(light.Direction.xyz));
        float inner    = cos(light.InnerConeAngle);
        float outer    = cos(light.OuterConeAngle);
        float cone     = saturate((cosAngle - outer) / max(inner - outer, 0.001f));
        attenuation   *= cone * cone;
    }

    if (attenuation <= 0.0f) return float3(0, 0, 0);

    float  NdotL    = max(dot(N, L), 0.0f);
    float3 diffuse  = NdotL * albedo * radiance * attenuation;
    float3 H        = normalize(L + V);
    float  NdotH    = max(dot(N, H), 0.0f);
    float3 specular = pow(NdotH, 32.0f) * 0.3f * radiance * attenuation;

    return diffuse + specular;
}

float4 LightingPS(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float4 albedo4   = gAlbedo.Sample  (gSamplerL, uv);
    float4 normal4   = gNormal.Sample  (gSamplerL, uv);
    float4 worldPos4 = gWorldPos.Sample(gSamplerL, uv);

    if (worldPos4.w < 0.01f)
        return float4(0.05f, 0.08f, 0.15f, 1.0f);

    if (albedo4.a < 0.5f)
    {
        float3 emissive = albedo4.rgb * 3.0f;
        emissive = emissive / (emissive + 1.0f);
        emissive = pow(abs(emissive), 1.0f / 2.2f);
        return float4(emissive, 1.0f);
    }

    float3 albedo   = albedo4.rgb;
    float3 N        = normalize(normal4.xyz * 2.0f - 1.0f);
    float3 worldPos = worldPos4.xyz;
    float3 V        = normalize(gCameraPos_L.xyz - worldPos);
    float3 color    = albedo * 0.08f;

    int count = clamp(gLightCount, 0, 64);
    for (int i = 0; i < count; ++i)
        color += CalcLight(gLights[i], albedo, N, V, worldPos);

    color = color / (color + 1.0f);
    color = pow(abs(color), 1.0f / 2.2f);

    return float4(color, 1.0f);
}
