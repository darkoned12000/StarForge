// Bloom bright-pass: scene (full res) -> bloomA (half res).
// 2x2 box downsample with Karis weighting + soft-knee threshold.
//
// TextureEffect layout: 0 = input image, 1 = sampler. Push: 16 bytes.
//
// ORIENTATION RULE for every post pass in this project: the scene, bloom and
// resolve passes all draw with a flipped viewport (height < 0), so output
// framebuffer row r corresponds to NDC y = +1 at r = 0. To make output row r
// read input row r (no flip, in any pass), V must be 0.5 - 0.5 * y. If ANY
// pass in the chain uses 0.5 + 0.5 * y instead, that pass mirrors the image
// vertically, and an odd number of such passes shows up as a mirrored bloom.

struct PushConstants
{
    float threshold; // brightness (max channel) where bloom starts, e.g. 1.0
    float knee;      // soft-knee width; <= 0 falls back to 0.5
    float pad0;
    float pad1;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Texture2D<float4> srcTex;
[[vk::binding(1, 0)]] SamplerState srcSampler;

struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput out;
    float2 p = positions[vertexIndex];
    out.position = float4(p, 0.0, 1.0);
    out.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); // flipped viewport: row r -> row r
    return out;
}

float3 prefilter(float3 c, float knee)
{
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - pc.threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-4);
    return c * contribution;
}

float karis(float3 c)
{
    return 1.0 / (1.0 + dot(c, float3(0.2126, 0.7152, 0.0722)));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    uint w, h;
    srcTex.GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float knee = pc.knee > 0.0 ? pc.knee : 0.5;

    // Four bilinear taps around the output pixel centre (covers a 4x4 source area).
    float3 a = prefilter(srcTex.SampleLevel(srcSampler, input.uv + texel * float2(-1.0, -1.0), 0).rgb, knee);
    float3 b = prefilter(srcTex.SampleLevel(srcSampler, input.uv + texel * float2( 1.0, -1.0), 0).rgb, knee);
    float3 c = prefilter(srcTex.SampleLevel(srcSampler, input.uv + texel * float2(-1.0,  1.0), 0).rgb, knee);
    float3 d = prefilter(srcTex.SampleLevel(srcSampler, input.uv + texel * float2( 1.0,  1.0), 0).rgb, knee);

    float wa = karis(a), wb = karis(b), wc = karis(c), wd = karis(d);
    float3 result = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
    return float4(result, 1.0);
}
