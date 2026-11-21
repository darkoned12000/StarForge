// Separable 9-tap Gaussian (5 bilinear fetches) on the half-res bloom targets.
//
// TextureEffect layout: 0 = input image, 1 = sampler. Push: 16 bytes
// {texelX, texelY, direction (0 = horizontal, 1 = vertical), pad}.
// Same orientation rule as hdr_bright.slang: V = 0.5 - 0.5 * y under the
// flipped viewport, so this pass never changes the image's orientation.

struct PushConstants
{
    float2 texel;
    float direction;
    float pad;
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
    out.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Linear-sampled Gaussian: offsets/weights fold 9 taps into 5 fetches.
    static const float offsets[3] = { 0.0, 1.3846153846, 3.2307692308 };
    static const float weights[3] = { 0.2270270270, 0.3162162162, 0.0702702703 };

    float2 axis = (pc.direction < 0.5) ? float2(pc.texel.x, 0.0) : float2(0.0, pc.texel.y);

    float3 sum = srcTex.SampleLevel(srcSampler, input.uv, 0).rgb * weights[0];
    for (int i = 1; i < 3; ++i)
    {
        float2 o = axis * offsets[i];
        sum += srcTex.SampleLevel(srcSampler, input.uv + o, 0).rgb * weights[i];
        sum += srcTex.SampleLevel(srcSampler, input.uv - o, 0).rgb * weights[i];
    }
    return float4(sum, 1.0);
}
