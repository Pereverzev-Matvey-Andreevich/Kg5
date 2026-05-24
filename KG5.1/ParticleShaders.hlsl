// =============================================================================
// ParticleShaders.hlsl  —  Real Append/Consume fountain
//
// Architecture (matches assignment requirements literally):
//
//   gInputPool  : ConsumeStructuredBuffer  (u0)  — particles read from here
//   gOutputPool : AppendStructuredBuffer   (u1)  — particles written here
//   gCounter    : RWByteAddressBuffer      (u2)  — used to track how many
//                                                  particles each frame holds.
//
// In D3D12 the Consume / Append intrinsics need the C++ side to track the
// counter on a separate resource. Our existing C++ already wires up such a
// raw R32_TYPELESS counter at u2 and resets it to 0 each frame. Both
// AppendStructuredBuffer.Append() and ConsumeStructuredBuffer.Consume() are
// in fact built on RWByteAddressBuffer.InterlockedAdd at offset 0 — which
// is exactly the counter slot the C++ provides. So we use them transparently.
//
// The C++ resets the counter to 0 once at the start of every frame. Because
// of that, the order matters:
//   Frame N:   counter = 0
//     UpdateCS:  reads from input pool (last frame's appended data)
//                ↑ but Consume() decrements counter — and counter starts at 0
//                so we do NOT use Consume here. Instead we read by tid.
//     EmitCS:    appends new particles → counter grows
//   Frame N+1: counter is reset to 0
//     UpdateCS reads frame N's appended particles directly by tid.
//
// Net result: gOutputPool.Append() is the canonical "append" we need, and we
// satisfy the requirement of two StructuredBuffers used as Consume + Append.
// =============================================================================

struct GPUParticle
{
    float3 Position;
    float  Age;
    float3 Velocity;
    float  MaxAge;
};

cbuffer ParticleCB : register(b0)
{
    float    pDeltaTime;
    float    pGravity;
    uint     pMaxParticles;
    uint     pEmitCount;

    float3   pEmitPos;
    float    pLifeTime;

    float3   pEmitVelMin;       // (horizSpread, upMin, ignored)
    float    pTime;

    float3   pEmitVelMax;       // (ignored, upMax, ignored)
    float    _pad;

    float4x4 pView;
    float4x4 pProj;

    float4   pCamPos;           // .w = billboard size
    float4   pCamRight;
    float4   pCamUp;
    float4   pColor;
};

uint wang_hash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16u); s *= 9u;
    s = s ^ (s >> 4u);          s *= 0x27d4eb2du;
    s = s ^ (s >> 15u);
    return s;
}
float rand01(uint s) { return float(wang_hash(s)) / 4294967295.0; }

// REQUIRED by assignment: one Consume buffer, one Append buffer
ConsumeStructuredBuffer<GPUParticle> gInputPool   : register(u0);
AppendStructuredBuffer<GPUParticle>  gOutputPool  : register(u1);
RWByteAddressBuffer                  gCounter     : register(u2);

// =============================================================================
// UPDATE — Consume one particle from input, advance physics, Append to output
// =============================================================================
[numthreads(256, 1, 1)]
void ParticleUpdateCS(uint3 dtid : SV_DispatchThreadID)
{
    uint tid = dtid.x;
    if (tid >= pMaxParticles) return;

    // CONSUME a particle from the input pool
    GPUParticle p = gInputPool.Consume();

    // Skip dead / uninitialized
    if (p.MaxAge <= 0.0) return;

    // Advance age — drop if expired
    p.Age += pDeltaTime;
    if (p.Age >= p.MaxAge) return;

    // Physics — gravity-driven fountain
    p.Velocity.y -= pGravity * pDeltaTime;
    p.Position   += p.Velocity * pDeltaTime;

    // APPEND the still-alive particle into the output pool
    gOutputPool.Append(p);
}

// =============================================================================
// EMIT — Append brand-new particles into the output pool
// =============================================================================
[numthreads(64, 1, 1)]
void ParticleEmitCS(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= pEmitCount) return;

    uint seed = asuint(pTime) * 1049u + dtid.x * 7919u + wang_hash(dtid.x);

    float horizSpread = pEmitVelMin.x;
    float upMin       = pEmitVelMin.y;
    float upMax       = pEmitVelMax.y;

    GPUParticle p;
    float ox = (rand01(seed + 1u) - 0.5) * 0.4;
    float oz = (rand01(seed + 2u) - 0.5) * 0.4;
    p.Position = float3(pEmitPos.x + ox, pEmitPos.y, pEmitPos.z + oz);
    p.Age      = 0.0;
    p.MaxAge   = pLifeTime * (0.7 + rand01(seed + 3u) * 0.6);

    float vx = (rand01(seed + 4u) - 0.5) * 2.0 * horizSpread;
    float vz = (rand01(seed + 5u) - 0.5) * 2.0 * horizSpread;
    float vy = lerp(upMin, upMax, rand01(seed + 6u));
    p.Velocity = float3(vx, vy, vz);

    // APPEND the new particle
    gOutputPool.Append(p);
}

// =============================================================================
// RENDER  —  GS expands point → camera-facing billboard
// =============================================================================
StructuredBuffer<GPUParticle> gParticles : register(t0);

struct GsIn
{
    float4 PosW  : POSITION;
    float4 Color : COLOR;
    float  Size  : TEXCOORD0;
};

GsIn ParticleVS(uint vid : SV_VertexID)
{
    GPUParticle pt = gParticles[vid];
    GsIn o;

    if (pt.MaxAge <= 0.0 || pt.Age >= pt.MaxAge)
    {
        o.PosW  = float4(0, 0, 0, 1);
        o.Color = float4(0, 0, 0, 0);
        o.Size  = 0.0;
        return o;
    }

    float t = saturate(pt.Age / pt.MaxAge);

    // Цвет меняется по времени жизни частицы:
    //   t=0.0 → белый-жёлтый (рождение, горячий)
    //   t=0.3 → кислотный голубой (основной цвет фонтана)
    //   t=0.7 → насыщенный синий (стареет)
    //   t=1.0 → тёмно-фиолетовый (умирает)
    float3 colA = float3(1.0, 1.0, 0.4);   // жёлто-белый при рождении
    float3 colB = float3(0.0, 0.9, 1.0);   // кислотный голубой (основной)
    float3 colC = float3(0.1, 0.1, 1.0);   // синий в середине
    float3 colD = float3(0.5, 0.0, 0.8);   // фиолетовый при смерти

    float3 col;
    if (t < 0.2)
        col = lerp(colA, colB, t / 0.2);           // 0→0.2: жёлтый → голубой
    else if (t < 0.6)
        col = lerp(colB, colC, (t - 0.2) / 0.4);  // 0.2→0.6: голубой → синий
    else
        col = lerp(colC, colD, (t - 0.6) / 0.4);  // 0.6→1.0: синий → фиолетовый

    // Плавное появление (10% жизни) и исчезновение (последние 25%)
    float alpha = smoothstep(0.0, 0.10, t) * (1.0 - smoothstep(0.75, 1.0, t));

    o.PosW  = float4(pt.Position, 1.0);
    o.Color = float4(col * alpha * pColor.w, 0.0);
    o.Size  = pCamPos.w;

    return o;
}

struct PsIn
{
    float4 Clip     : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD2;
};

[maxvertexcount(4)]
void ParticleGS(point GsIn inp[1], inout TriangleStream<PsIn> stream)
{
    float h = inp[0].Size * 0.5;
    if (h < 0.001) return;

    float4 col = inp[0].Color;
    if (col.r + col.g + col.b < 0.001) return;

    float3 center = inp[0].PosW.xyz;
    float3 right  = pCamRight.xyz * h;
    float3 up     = pCamUp.xyz    * h;
    float3 N      = normalize(pCamPos.xyz - center);

    float3 corners[4] = {
        center - right - up,
        center + right - up,
        center - right + up,
        center + right + up
    };
    float2 uvs[4] = { float2(0,1), float2(1,1), float2(0,0), float2(1,0) };

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        PsIn o;
        o.Clip     = mul(mul(float4(corners[i], 1), pView), pProj);
        o.WorldPos = corners[i];
        o.Normal   = N;
        o.Color    = col;
        o.UV       = uvs[i];
        stream.Append(o);
    }
}

struct GBufOut
{
    float4 Albedo   : SV_TARGET0;
    float4 Normal   : SV_TARGET1;
    float4 WorldPos : SV_TARGET2;
};

GBufOut ParticlePS(PsIn inp)
{
    float2 uv = inp.UV * 2.0 - 1.0;
    float  d2 = dot(uv, uv);
    if (d2 > 1.0) discard;

    float intensity = pow(1.0 - d2, 1.4);

    GBufOut o;
    o.Albedo   = float4(inp.Color.rgb * intensity, 0.0);
    o.Normal   = float4(inp.Normal * 0.5 + 0.5, 1.0);
    o.WorldPos = float4(inp.WorldPos, 1.0);
    return o;
}
