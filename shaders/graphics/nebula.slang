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
    output.position = float4(pos, 1.0, 1.0); // far plane in Vulkan [0,1] depth
    output.ndc = pos;
    return output;
}

uint hash32(uint x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

float hash31(int3 p)
{
    uint x = hash32(uint(p.x));
    x = hash32(x ^ uint(p.y));
    x = hash32(x ^ uint(p.z));
    return float(x) * (1.0 / 4294967296.0);
}

float valueNoise3(float3 p)
{
    int3 i = int3(floor(p));
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash31(i);
    float b = hash31(i + int3(1, 0, 0));
    float c = hash31(i + int3(0, 1, 0));
    float d = hash31(i + int3(1, 1, 0));
    float e = hash31(i + int3(0, 0, 1));
    float g = hash31(i + int3(1, 0, 1));
    float h = hash31(i + int3(0, 1, 1));
    float k = hash31(i + int3(1, 1, 1));
    return lerp(lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y), lerp(lerp(e, g, f.x), lerp(h, k, f.x), f.y), f.z);
}

float fbm3(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int o = 0; o < 4; ++o)
    {
        v += amp * valueNoise3(p);
        p = p * 2.03 + float3(19.7, 7.3, 5.1);
        amp *= 0.5;
    }
    return v;
}

// Seam-free: FBM sampled directly on the sphere (3D noise), so there
// are no octahedral fold edges to see as straight border lines.

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.aspectTan.x * pc.aspectTan.y +
                           pc.up.xyz * input.ndc.y * pc.aspectTan.y +
                           pc.forward.xyz);

    float t = pc.time * 0.02;
    float3 q = float3(
        valueNoise3(dir * 6.0 + t),
        valueNoise3(dir * 6.0 - t + float3(17.3, 9.2, 4.1)),
        valueNoise3(dir * 6.0 + float3(8.4, 3.7, 12.9)));
    float3 warp = float3(
        valueNoise3(dir * 8.0 + q * 2.0 + t),
        valueNoise3(dir * 8.0 - q * 2.0 - t + float3(11.1, 4.3, 7.7)),
        valueNoise3(dir * 8.0 + q * 1.5 + float3(5.2, 13.6, 9.9)));
    float dens = fbm3(dir * 7.0 + warp * 1.5 + t);

    float cloud = smoothstep(0.35, 0.85, dens) * pc.density;
    // Self-absorption attenuates deep layers.
    cloud *= exp(-cloud * pc.absorption);
    float3 col = pc.color.rgb * cloud * pc.scattering;
    col += pc.color.rgb * cloud * cloud * 0.3; // core brightening (no channel swizzle)

    return float4(col, 0.0); // Additive (ONE, ONE); no background term.
}
