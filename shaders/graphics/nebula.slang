// Volumetric-look nebula: world-locked FBM sampled along the view
// direction (octahedral mapped), so it stays pinned when rotating.
// Push: time, density, scattering, absorption | color(vec4) |
//       resolution | invViewProj = 112B.

struct PushConstants
{
    float time;
    float density;
    float scattering;
    float absorption;
    float4 color;
    float2 resolution;
    float4x4 invViewProj;
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput out;
    float2 pos = positions[vertexId];
    out.position = float4(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

float hash21(float2 p)
{
    uint2 u = uint2(asuint(p.x), asuint(p.y));
    u = u * 1664525u + 1013904223u;
    u.x += u.y * 1664525u;
    u.y += u.x * 1664525u;
    u = u ^ (u >> 16u);
    u.x += u.y * 1664525u;
    return float(u.x) * (1.0 / 4294967296.0);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
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
    float4 clip = float4(input.uv * 2.0 - 1.0, 1.0, 1.0);
    float4 worldRay = mul(pc.invViewProj, clip);
    float3 dir = normalize(worldRay.xyz / max(worldRay.w, 1e-5));
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
