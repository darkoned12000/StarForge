// M8 residual / M15.0.6 botanical: nebula FBM driven by a seeded 64^3 3D noise
// volume instead of ~300 hash evaluations per pixel. The same world-locked
// view-direction construction as nebula.slang keeps it seamless and pinned.
[[vk::binding(0, 0)]] Texture3D<float> noiseVolume;
[[vk::binding(1, 0)]] SamplerState noiseSampler;

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

struct VSOutput { float4 position : SV_Position; float2 ndc : TEXCOORD0; };

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

float noise3(float3 p)
{
    return noiseVolume.Sample(noiseSampler, frac(p)).r;
}

float fbm3(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int o = 0; o < 4; ++o)
    {
        v += amp * noise3(p);
        p = p * 2.03 + float3(0.19, 0.07, 0.05);
        amp *= 0.5;
    }
    return v;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.aspectTan.x * pc.aspectTan.y +
                           pc.up.xyz * input.ndc.y * pc.aspectTan.y +
                           pc.forward.xyz);
    float t = pc.time * 0.02;
    float3 q = float3(noise3(dir * 0.6 + t),
                      noise3(dir * 0.6 - t + float3(0.17, 0.09, 0.04)),
                      noise3(dir * 0.6 + float3(0.08, 0.03, 0.12)));
    float3 warp = float3(noise3(dir * 0.8 + q * 0.2 + t),
                         noise3(dir * 0.8 - q * 0.2 - t + float3(0.11, 0.04, 0.07)),
                         noise3(dir * 0.8 + q * 0.15 + float3(0.05, 0.13, 0.09)));
    float dens = fbm3(dir * 0.7 + warp * 0.15 + t);
    float cloud = smoothstep(0.28, 0.72, dens) * pc.density;
    if (pc.aspectTan.w > 0.5)
        cloud = floor(cloud * 5.0 + 0.5) / 5.0;
    cloud *= exp(-cloud * pc.absorption);
    float3 col = pc.color.rgb * cloud * pc.scattering;
    col += pc.color.rgb * cloud * cloud * 0.3;
    // Alpha-composited so the cloud occludes/dims stars behind it.
    float opacity = clamp(cloud, 0.0f, 0.85f);
    return float4(col * 1.35f, opacity);
}
