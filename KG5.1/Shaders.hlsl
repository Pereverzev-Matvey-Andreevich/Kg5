// ================================================================
// Shaders.hlsl  -  Phong lighting + texture + tiling + UV animation
// ================================================================

// ---- Constant buffer (b0) ----
cbuffer ConstantBuffer : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Proj;
    float4   LightPos;
    float4   LightColor;
    float4   CameraPos;
    float2   Tiling;    // texture tiling factor  (set from CPU)
    float2   UVOffset;  // animated scroll offset (set from CPU)
};

// ---- Textures + Sampler ----
Texture2D    gTexture  : register(t0);  // текстура A
Texture2D    gTexture2 : register(t1);  // текстура B
SamplerState gSampler  : register(s0);

// ================================================================
// Vertex Shader I/O
// ================================================================
struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct PSInput
{
    float4 ClipPos  : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD2;
    float2 RawUV    : TEXCOORD3;  // оригинальный UV без тайлинга
};

// ================================================================
// Vertex Shader
// ================================================================
PSInput VSMain(VSInput input)
{
    PSInput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    float4 viewPos  = mul(worldPos, View);
    float4 clipPos  = mul(viewPos,  Proj);

    output.ClipPos  = clipPos;
    output.WorldPos = worldPos.xyz;
    output.Normal   = normalize(mul(float4(input.Normal, 0.0f), World).xyz);
    output.Color    = input.Color;
    output.TexCoord = input.TexCoord * Tiling + UVOffset;
    output.RawUV    = input.TexCoord;

    return output;
}

// ================================================================
// Pixel Shader  -  Phong + chess texture
// ================================================================
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    float3 L = normalize(LightPos.xyz  - input.WorldPos);
    float3 V = normalize(CameraPos.xyz - input.WorldPos);
    float3 R = reflect(-L, N);

    // ---- Ambient ----
    float  ambientStrength = 0.15f;
    float3 ambient = ambientStrength * LightColor.rgb;

    // ---- Diffuse (Lambertian) ----
    float  diff    = max(dot(N, L), 0.0f);
    float3 diffuse = diff * LightColor.rgb;

    // ---- Specular (Phong) ----
    float  shininess = 64.0f;
    float  spec      = pow(max(dot(V, R), 0.0f), shininess);
    float3 specular  = 0.6f * spec * LightColor.rgb;

    // ================================================================
    // Шахматный паттерн: каждая клетка целиком заполнена своей текстурой.
    //
    // CHECKER_COUNT — число клеток по каждой оси. Меняйте только здесь.
    // ================================================================
    static const float CHECKER_COUNT = 8.0f;

    // scaled: координата в пространстве клеток (0..CHECKER_COUNT)
    float2 scaled  = input.RawUV * CHECKER_COUNT;

    // cell: номер клетки по X и Y
    int2   cell    = int2(floor(scaled));

    // localUV: UV внутри одной клетки от [0,0] до [1,1]
    float2 localUV = frac(scaled);

    // Чётные клетки (0,0)(1,1)(2,0)... → texture1
    // Нечётные клетки (0,1)(1,0)(2,1)... → texture2
    bool useSecond = ((cell.x + cell.y) & 1) != 0;

    float4 texColor = useSecond
        ? gTexture2.Sample(gSampler, localUV)
        : gTexture.Sample (gSampler, localUV);

    // ---- Combine: lighting * vertex color * texture ----
    float3 lighting = ambient + diffuse + specular;
    float3 result   = lighting * input.Color.rgb * texColor.rgb;

    return float4(result, 1.0f);
}
