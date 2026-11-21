// Volumetric-look nebula: world-locked FBM sampled along the view direction,
// so it stays pinned when rotating. Push: time/density/scattering/absorption |
// color | camera basis + {aspect, tanHalfFov} = 96 bytes.
// The cloud is alpha-composited over the starfield: a multi-hue palette,
// bright cores, and multiplicative dust lanes give the layered, painterly
// depth of the concept art instead of a single flat hue.

#include "output_shoulder.slang"

struct PushConstants
{
    float time;
    float density;
    float scattering;
    float absorption;
    float4 color;
    float4 right;
    float4 up;
    float4 forward;
    float4 aspectTan;
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput output;
    float2 pos = positions[vertexId];
    output.position = float4(pos, 1.0, 1.0);
    output.ndc = pos;
    return output;
}

uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float hash31(int3 p)
{
    uint u = pcgHash(uint(p.x));
    u = pcgHash(u ^ uint(p.y) + 0x9e3779b9u);
    u = pcgHash(u ^ uint(p.z) + 0x85ebca6bu);
    return float(u) * (1.0 / 4294967296.0);
}

// Cheap value noise for warp / region / dust fields (1 hash per corner).
float valueNoise3(float3 p)
{
    int3 i = int3(floor(p));
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(hash31(i), hash31(i + int3(1,0,0)), f.x),
                     lerp(hash31(i + int3(0,1,0)), hash31(i + int3(1,1,0)), f.x), f.y),
                lerp(lerp(hash31(i + int3(0,0,1)), hash31(i + int3(1,0,1)), f.x),
                     lerp(hash31(i + int3(0,1,1)), hash31(i + int3(1,1,1)), f.x), f.y), f.z);
}

// Gradient (Perlin-style) noise: soft gradients give wispy filaments rather
// than the blobs of plain value noise.
float3 grad3(int3 cell)
{
    return normalize(float3(hash31(cell), hash31(cell + int3(17, 7, 31)),
                            hash31(cell + int3(53, 13, 3))) * 2.0f - 1.0f);
}

float gradientNoise3(float3 p)
{
    int3 i = int3(floor(p));
    float3 f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float a = dot(grad3(i + int3(0,0,0)), f - float3(0,0,0));
    float b = dot(grad3(i + int3(1,0,0)), f - float3(1,0,0));
    float c = dot(grad3(i + int3(0,1,0)), f - float3(0,1,0));
    float d = dot(grad3(i + int3(1,1,0)), f - float3(1,1,0));
    float e = dot(grad3(i + int3(0,0,1)), f - float3(0,0,1));
    float j = dot(grad3(i + int3(1,0,1)), f - float3(1,0,1));
    float k = dot(grad3(i + int3(0,1,1)), f - float3(0,1,1));
    float l = dot(grad3(i + int3(1,1,1)), f - float3(1,1,1));
    return lerp(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y),
                lerp(lerp(e, j, u.x), lerp(k, l, u.x), u.y), u.z) * 0.9f + 0.5f;
}

float fbm3(float3 p)
{
    float v = 0.0f;
    float amp = 0.5f;
    for (int o = 0; o < 4; ++o)
    {
        v += amp * gradientNoise3(p);
        // Ridged octave adds thin bright filaments on top of the soft base.
        if (o == 2) v += amp * 0.25f * (1.0f - abs(gradientNoise3(p * 1.7f) * 2.0f - 1.0f));
        p = p * 2.07f + float3(19.7f, 7.3f, 5.1f);
        amp *= 0.5f;
    }
    return v;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.aspectTan.x * pc.aspectTan.y +
                           pc.up.xyz * input.ndc.y * pc.aspectTan.y +
                           pc.forward.xyz);

    float t = pc.time * 0.02f;
    float3 q = float3(valueNoise3(dir * 5.0f + t),
                      valueNoise3(dir * 5.0f - t + float3(17.3f, 9.2f, 4.1f)),
                      valueNoise3(dir * 5.0f + float3(8.4f, 3.7f, 12.9f)));
    float3 warp = float3(valueNoise3(dir * 8.0f + q * 2.0f + t),
                         valueNoise3(dir * 8.0f - q * 2.0f - t + float3(11.1f, 4.3f, 7.7f)),
                         valueNoise3(dir * 8.0f + q * 1.5f + float3(5.2f, 13.6f, 9.9f)));
    float dens = fbm3(dir * 6.5f + warp * 1.8f + t);

    float cloud = smoothstep(0.34f, 0.82f, dens) * pc.density;
    cloud *= exp(-cloud * pc.absorption);

    // Multi-hue region map: the caller's colour is the base, with cool cyan
    // mid regions and warm amber hot cores blended by a low-frequency field.
    float region = valueNoise3(dir * 2.0f + float3(31.0f, 5.0f, 17.0f));
    float3 cool = float3(0.12f, 0.55f, 0.95f);
    float3 warm = float3(1.0f, 0.48f, 0.18f);
    float3 tint = lerp(lerp(pc.color.rgb, cool, smoothstep(0.30f, 0.62f, region)),
                       warm, smoothstep(0.62f, 0.86f, region));

    float3 col = tint * cloud * pc.scattering;
    col += float3(1.0f, 0.88f, 0.74f) * pow(cloud, 3.0f) * 0.45f; // hot near-white cores

    // Multiplicative dust lanes carve dark filaments through the gas.
    float dust = smoothstep(0.52f, 0.80f, valueNoise3(dir * 9.0f + warp * 2.2f + float3(7.0f, 3.0f, 11.0f)));
    col *= 1.0f - dust * 0.75f;

    float opacity = saturate(clamp(cloud, 0.0f, 0.72f) + dust * 0.22f);
    return float4(outputColor(col * 0.95f), opacity);
}
