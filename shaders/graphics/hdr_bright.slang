// Bright-pass extraction for bloom.
[[vk::binding(0, 0)]] Texture2D<float4> inputTexture;
[[vk::binding(1, 0)]] SamplerState inputSampler;
struct Push { float4 params; }; // x = threshold
[[vk::push_constant]] Push pushData;
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    float2 pos = float2((vertexIndex == 1u) ? 3.0 : -1.0, (vertexIndex == 2u) ? 3.0 : -1.0);
    VSOutput out;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 color = inputTexture.Sample(inputSampler, input.uv).rgb;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    float contribution = max(luminance - pushData.params.x, 0.0) / max(luminance, 1e-4);
    return float4(color * contribution, 1.0);
}
