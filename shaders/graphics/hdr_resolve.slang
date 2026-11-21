// HDR resolve: linear scene colour -> tonemapped swapchain pixel.
// Adapted from Claude's hdr/tonemap.slang to the project's TextureEffect
// descriptor layout (0 = input image, 1 = sampler, 2 = second image,
// 3 = sampler). The second image is the bloom source; it is currently bound
// to the scene image (bloom strength 0) until the bloom chain lands.
//
// The swapchain is an *_SRGB format, so the hardware applies the sRGB
// transfer function on write: this shader outputs LINEAR values and must not
// apply gamma itself. The only exception is the dither, done in an
// approximate gamma space on purpose (one 8-bit code step near black is much
// larger in gamma space than in linear light).
//
// Push: 32 bytes.

struct PushConstants
{
    float exposure;       // linear multiplier before tonemapping (1.0 neutral)
    float bloomStrength;  // 0 disables the bloom contribution
    float vignette;       // 0..~0.5 corner darkening
    uint  mode;           // 0 = ACES (Narkowicz), 1 = Khronos PBR Neutral
    uint  frame;          // animates the dither pattern
    float pad0;
    float pad1;
    float pad2;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Texture2D<float4> sceneTex;
[[vk::binding(1, 0)]] SamplerState sceneSampler;
[[vk::binding(2, 0)]] Texture2D<float4> bloomTex;
[[vk::binding(3, 0)]] SamplerState bloomSampler;

struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput out;
    float2 p = positions[vertexIndex];
    out.position = float4(p, 0.0, 1.0);
    // Both scene and resolve passes use Vulkan's flipped-Y viewport (height
    // < 0), so the offscreen image is stored top-down in framebuffer rows.
    // Sampling with the usual p*0.5+0.5 would flip it vertically: the ship
    // appeared upside down and the sky mirrored. Flip V to compensate.
    out.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

float3 tonemapACES(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 tonemapPBRNeutral(float3 color)
{
    // Khronos PBR Neutral: leaves colours below ~0.8 untouched, preserving
    // the stylized palette; only highlights compress toward white.
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1.0f - startCompression;
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, newPeak * float3(1.0f, 1.0f, 1.0f), g);
}

// Interleaved gradient noise (Jimenez 2014): cheap, temporally stable.
float ign(float2 pixel, uint frame)
{
    pixel += 5.588238f * float(frame & 63u);
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 hdr = sceneTex.SampleLevel(sceneSampler, input.uv, 0).rgb;
    // Bloom passes use the same flipped-viewport V convention as scene/resolve,
    // so bloom is already orientation-matched to the scene.
    float3 bloom = bloomTex.SampleLevel(bloomSampler, input.uv, 0).rgb;

    float3 color = (hdr + bloom * pc.bloomStrength) * pc.exposure;

    float2 centered = input.uv * 2.0 - 1.0;
    float vig = 1.0 - pc.vignette * dot(centered, centered) * 0.5;
    color *= saturate(vig);

    color = (pc.mode == 0u) ? tonemapACES(color) : tonemapPBRNeutral(color);

    float3 g = pow(max(color, 0.0), 1.0 / 2.2);
    g += (ign(input.position.xy, pc.frame) - 0.5) / 255.0;
    color = pow(max(g, 0.0), 2.2);
    return float4(color, 1.0);
}
