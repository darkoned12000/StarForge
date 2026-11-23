// M17.5: instanced combat effects in the HDR scene target.
//
// One draw for every bolt, spark, shockwave, explosion layer and engine
// plume. Instances are camera-relative world data read through BDA; the
// output is additive and deliberately exceeds 1.0 so the bloom pass picks
// up the hot cores. No blending/depth sorting between instances is needed.
#include "output_shoulder.slang"

struct InstanceData
{
    float4 posRadius;  // xyz camera-relative position, w world radius
    float4 direction;  // xyz beam/plume axis (0 for billboard), w kind
    float4 color;      // rgb HDR colour, a intensity
    float4 misc;       // x age/phase, y seed, z aspect, w fade
};

struct Push
{
    float4x4 viewProj;        // 0
    float4 cameraRight;       // 64
    float4 cameraUp;          // 80
    float4 viewport;          // 96: xy resolution
    uint64_t instanceAddress; // 112
};
[[vk::push_constant]] Push pc;

typedef vk::BufferPointer<InstanceData> InstancePtr;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
    float intensity : TEXCOORD1;
    float4 misc : TEXCOORD2;
    float kind : TEXCOORD3;
};

// Cheap 2D value noise; deterministic from the instance seed, so goldens
// are stable and no persistent RNG state is needed.
float hash21(float2 p, float seed)
{
    return frac(sin(dot(p, float2(127.1, 311.7)) + seed * 0.371) * 43758.5453);
}

float valueNoise(float2 p, float seed)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i, seed);
    float b = hash21(i + float2(1.0, 0.0), seed);
    float c = hash21(i + float2(0.0, 1.0), seed);
    float d = hash21(i + float2(1.0, 1.0), seed);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float4 cornerFor(uint vertexId)
{
    // 6-vertex quad: two triangles, UV in [-1,1].
    float2 c0 = float2(-1.0, -1.0);
    float2 c1 = float2( 1.0, -1.0);
    float2 c2 = float2( 1.0,  1.0);
    float2 c3 = float2(-1.0,  1.0);
    float2 c = vertexId == 0u ? c0 : vertexId == 1u ? c1 : vertexId == 2u ? c2
             : vertexId == 3u ? c0 : vertexId == 4u ? c2 : c3;
    return float4(c, 0.0, 1.0);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    InstancePtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();
    float2 c = cornerFor(vertexId).xy;

    float3 right = pc.cameraRight.xyz;
    float3 up = pc.cameraUp.xyz;
    float3 axis = inst.direction.xyz;
    float kind = inst.direction.w;

    float3 world;
    if (kind < 0.5)
    {
        // Beam / debris trail: camera-facing ribbon along the world axis.
        // cameraPosition is packed in viewport.zw + cameraRight.w. Never
        // normalize a zero vector: an enemy bolt crossing the ship origin
        // would produce NaN and poison the additive bloom chain.
        float3 camPos = float3(pc.viewport.z, pc.viewport.w, pc.cameraRight.w);
        float3 toCam = camPos - inst.posRadius.xyz;
        const float len2 = dot(toCam, toCam);
        float3 along = normalize(axis + float3(1e-5, 0.0, 0.0));
        float3 viewDir = len2 > 1e-6f ? toCam * rsqrt(len2) : -along;
        float3 sideRaw = cross(along, viewDir);
        float3 side = dot(sideRaw, sideRaw) > 1e-6f ? normalize(sideRaw) : pc.cameraRight.xyz;
        world = inst.posRadius.xyz + along * (c.y * inst.posRadius.w) + side * (c.x * inst.posRadius.w * 0.22);
    }
    else if (kind > 3.5)
    {
        // Engine plume: billboard aligned with the exhaust axis projected
        // into the image plane, so it trails behind the nozzle at any ship
        // pitch/roll instead of always pointing down the screen.
        float2 dir2 = float2(dot(axis, right), dot(axis, up));
        if (dot(dir2, dir2) < 1e-6f) dir2 = float2(0.0, -1.0);
        dir2 = normalize(dir2);
        float2 perp = float2(-dir2.y, dir2.x);
        float s = c.x * inst.posRadius.w;
        float t = c.y * inst.posRadius.w * inst.misc.z;
        world = inst.posRadius.xyz +
                right * (perp.x * s + dir2.x * t - dir2.x * inst.posRadius.w * 0.25f) +
                up * (perp.y * s + dir2.y * t - dir2.y * inst.posRadius.w * 0.25f);
    }
    else
    {
        // Billboard disc, ring or explosion.
        world = inst.posRadius.xyz + right * (c.x * inst.posRadius.w) +
                up * (c.y * inst.posRadius.w);
    }

    VSOutput out;
    out.position = mul(pc.viewProj, float4(world, 1.0));
    out.uv = c;
    out.color = inst.color.rgb;
    out.intensity = inst.color.a;
    out.misc = inst.misc;
    out.kind = kind;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;
    float r = length(uv);
    float age = input.misc.x;
    float seed = input.misc.y;
    float fade = input.misc.w;
    float shape = 0.0;

    if (input.kind < 0.5)
    {
        // Bolt: thin white-hot core inside a coloured halo, with soft caps.
        float core = exp(-uv.x * uv.x * 34.0);
        float halo = exp(-uv.x * uv.x * 5.0);
        float caps = smoothstep(1.0, 0.72, abs(uv.y));
        shape = (core * 1.8 + halo * 0.55) * caps;
        // The centre of the beam is driven towards white by the caller via
        // a high colour value; keep the halo coloured.
        input.color = lerp(input.color, float3(1.0, 1.0, 1.0), core * 0.75);
    }
    else if (input.kind < 1.5)
    {
        // Spark / smoke puff: soft disc with a hashed breakup.
        float n = valueNoise(uv * 3.5 + float2(age * 2.0, seed), seed);
        shape = smoothstep(1.0, 0.05, r) * (0.55 + 0.75 * n);
    }
    else if (input.kind < 2.5)
    {
        // Shockwave ring: thin, noisy annulus expanding with age.
        float n = valueNoise(uv * 6.0 + float2(seed, age * 4.0), seed);
        float ring = smoothstep(0.22, 0.02, abs(r - 0.78 + n * 0.09));
        shape = ring * smoothstep(1.0, 0.85, r);
    }
    else if (input.kind < 3.5)
    {
        // Explosion fireball: expanding noisy disc with a bright core.
        float n = valueNoise(uv * 4.5 + float2(seed, age * 1.5), seed);
        float body = smoothstep(1.0, 0.15, r + (n - 0.5) * 0.35);
        float core = exp(-r * r * 14.0);
        shape = body * (0.7 + 0.6 * n) + core * 1.8;
        input.color = lerp(input.color, float3(1.0, 0.95, 0.8), core * 0.7);
    }
    else
    {
        // Engine plume: compact flame glow. A tight white-hot core inside
        // a soft blue halo, with noise-scrolled turbulence and a slight
        // teardrop bias behind the nozzle. No long spikes, no bloom-out.
        float flow = valueNoise(uv * 3.0 + float2(0.0, -age * 6.0), seed);
        float flow2 = valueNoise(uv * 6.0 + float2(3.7, -age * 11.0), seed + 1.0);
        float turbulence = 0.62 + 0.38 * (flow * 0.65 + flow2 * 0.35);
        float halo = exp(-r * r * 3.2) * smoothstep(1.0, 0.55, r);
        float core = exp(-r * r * 18.0);
        float tail = smoothstep(0.6, -0.9, uv.y) * smoothstep(1.0, 0.2, r);
        shape = halo * turbulence * 0.75 + core * 1.0 + tail * 0.35;
        input.color = lerp(input.color, float3(1.0, 1.0, 1.0), core * 0.6);
    }

    float3 col = input.color * shape * input.intensity * fade;
    return float4(outputColor(col), 0.0);
}
