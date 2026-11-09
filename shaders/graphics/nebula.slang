// Volumetric-look nebula: world-locked FBM sampled along the view
// direction, so it stays pinned when rotating. The view direction is
// reconstructed from a camera basis, not by unprojecting a world
// position, so camera translation cannot shift the nebula.
// Push: time/density/scattering/absorption | color(vec4) | camera basis
//       (right/up/forward) + {aspect, tanHalfFov} = 96 bytes.
// Alpha is 0 because this effect is additive (ONE, ONE) with no
// background term; it must be drawn after the opaque sky/objects.

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
    output.position = float4(pos, 0.0, 1.0);
    output.ndc = pos;
    return output;
}

uint hash32(uint x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

float hash21(int2 p)
{
    uint2 u = uint2(uint(p.x), uint(p.y));
    u = u * 1664525u + 1013904223u;
    u.x += u.y * 1664525u;
    u.y += u.x * 1664525u;
    u = u ^ (u >> 16u);
    u.x += u.y * 1664525u;
    return float(u.x) * (1.0 / 4294967296.0);
}

float valueNoise(float2 p)
{
    int2 i = int2(floor(p));
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + int2(1, 0));
    float c = hash21(i + int2(0, 1));
    float d = hash21(i + int2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int o = 0; o < 4; ++o)
    {
        v += amp * valueNoise(p);
        p = p * 2.03 + float2(19.7, 7.3);
        amp *= 0.5;
    }
    return v;
}

// Seam-free octahedral position for a view direction.
float2 octahedral(float3 dir)
{
    float2 p = dir.xz / (1.0 + abs(dir.y));
    if (dir.y < 0.0)
    {
        p = sign(p) * (1.0 - abs(float2(p.y, p.x)));
    }
    return p;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.aspectTan.x * pc.aspectTan.y +
                           pc.up.xyz * input.ndc.y * pc.aspectTan.y +
                           pc.forward.xyz);
    float2 uv = octahedral(dir);

    float t = pc.time * 0.02;
    float2 q = float2(valueNoise(uv * 6.0 + t), valueNoise(uv * 6.0 - t));
    float2 warp = float2(valueNoise(uv * 8.0 + q * 2.0 + t), valueNoise(uv * 8.0 - q * 2.0 - t));
    float dens = fbm(uv * 7.0 + warp * 1.5 + t);

    float cloud = smoothstep(0.35, 0.85, dens) * pc.density;
    // Self-absorption attenuates deep layers.
    cloud *= exp(-cloud * pc.absorption);
    float3 col = pc.color.rgb * cloud * pc.scattering;
    col += pc.color.rgb * cloud * cloud * 0.3; // core brightening (no channel swizzle)

    return float4(col, 0.0); // Additive (ONE, ONE); no background term.
}
