// HDR resolve: scene colour (+ optional bloom) -> tonemapped swapchain pixel.
//
// Draw as a fullscreen triangle (3 vertices, no vertex buffer) inside the
// swapchain pass, BEFORE the UI. Pipeline state: no blending, no depth test
// and no depth write. The pass still has the swapchain depth attached, so
// declare the same depth format as the UI pipelines when used as a
// TextureEffect.
//
// The swapchain is an *_SRGB format: the hardware applies the sRGB transfer
// function on write, so this shader outputs LINEAR values and must not apply
// gamma itself.
//
// TextureEffect layout: binding 0 = scene HDR image, binding 1 = sampler,
// binding 2 = bloom image (or scene when bloom disabled), binding 3 = sampler.
// Push: 32 bytes.

struct PushConstants
{
    float exposure;      // linear multiplier before tonemapping (1.0 = neutral)
    float bloomStrength; // 0 disables bloom contribution
    float vignette;      // 0..~0.5 darkening at the corners
    uint  mode;          // 0 = ACES (Narkowicz fit), 1 = Khronos PBR Neutral
    uint  frame;         // frame counter, animates the dither pattern
    float pad0;
    float pad1;
    float pad2;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Texture2D<float4> hdrScene;
[[vk::binding(1, 0)]] SamplerState sceneSampler;
[[vk::binding(2, 0)]] Texture2D<float4> bloomTex;
[[vk::binding(3, 0)]] SamplerState bloomSampler;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput output;
    float2 p = positions[vertexId];
    output.position = float4(p, 0.0, 1.0);
    // Flipped viewport: V = 0.5 - 0.5*y keeps output row r reading input row r.
    output.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return output;
}

float3 tonemapACES(float3 x)
{
    // Narkowicz 2015 fit. Punchy, filmic; shifts saturated highlights toward white.
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 tonemapPBRNeutral(float3 color)
{
    // Khronos PBR Neutral: leaves colours below ~0.8 untouched, so the
    // stylised palette stays true; only highlights are compressed.
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak * float3(1.0, 1.0, 1.0), g);
}

// Interleaved gradient noise (Jimenez 2014), cheap and temporally stable.
float ign(float2 pixel, uint frame)
{
    pixel += 5.588238 * float(frame & 63u);
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 hdr = hdrScene.SampleLevel(sceneSampler, input.uv, 0).rgb;
    // Bloom passes follow the same flipped-viewport UV rule, so sample directly.
    float3 bloom = bloomTex.SampleLevel(bloomSampler, input.uv, 0).rgb;

    float3 color = (hdr + bloom * pc.bloomStrength) * pc.exposure;

    // Vignette in linear light, before tonemapping, so it also dims bloom.
    float2 centered = input.uv * 2.0 - 1.0;
    float vig = 1.0 - pc.vignette * dot(centered, centered) * 0.5;
    color *= saturate(vig);

    color = (pc.mode == 0u) ? tonemapACES(color) : tonemapPBRNeutral(color);

    // Dither to hide banding in dark nebula gradients on an 8-bit target.
    // Applied in an approximate gamma space: one 8-bit code step is ~1/255 in
    // sRGB but much larger in linear light near black, so dithering linear
    // values would add visible noise to the shadows.
    float3 g = pow(max(color, 0.0), 1.0 / 2.2);
    g += (ign(input.position.xy, pc.frame) - 0.5) / 255.0;
    color = pow(max(g, 0.0), 2.2);

    return float4(color, 1.0);
}
