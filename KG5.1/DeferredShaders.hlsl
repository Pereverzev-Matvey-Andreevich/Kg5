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
    float4   _GeomPad[3];
};

Texture2D    gTexture  : register(t0);
Texture2D    gTexture2 : register(t1);
SamplerState gSampler  : register(s0);

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
    static const float CHECKER = 8.0f;
    float2 scaled  = input.RawUV * CHECKER;
    int2   cell    = int2(floor(scaled));
    float2 localUV = frac(scaled);

    bool useB = ((cell.x + cell.y) & 1) != 0;
    float4 texColor = useB
        ? gTexture2.Sample(gSampler, localUV)
        : gTexture.Sample (gSampler, localUV);

    GeomPSOut o;
    o.Albedo   = float4(texColor.rgb * input.Color.rgb, input.Color.a);
    o.Normal   = float4(input.Normal * 0.5f + 0.5f, 1.0f);
    o.WorldPos = float4(input.WorldPos, 1.0f);
    return o;
}

cbuffer LightingCB : register(b0)
{
    float4    gCameraPos;
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

    float  NdotL   = max(dot(N, L), 0.0f);
    float3 diffuse = NdotL * albedo * radiance * attenuation;
    float3 H       = normalize(L + V);
    float  NdotH   = max(dot(N, H), 0.0f);
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
    float3 V        = normalize(gCameraPos.xyz - worldPos);
    float3 color    = albedo * 0.08f;

    int count = clamp(gLightCount, 0, 64);
    for (int i = 0; i < count; ++i)
        color += CalcLight(gLights[i], albedo, N, V, worldPos);

    color = color / (color + 1.0f);
    color = pow(abs(color), 1.0f / 2.2f);

    return float4(color, 1.0f);
}
