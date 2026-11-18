// Separable 9-tap Gaussian blur for bloom.
[[vk::binding(0, 0)]] Texture2D<float4> inputTexture;
[[vk::binding(1, 0)]] SamplerState inputSampler;
struct Push { float4 params; }; // xy = texel size, z = axis (0 = x, 1 = y)
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
    float2 axis = lerp(float2(pushData.params.x, 0.0), float2(0.0, pushData.params.y), pushData.params.z);
    float3 sum = inputTexture.Sample(inputSampler, input.uv).rgb * 0.227027;
    sum += inputTexture.Sample(inputSampler, input.uv + axis * 1.384615).rgb * 0.316216;
    sum += inputTexture.Sample(inputSampler, input.uv - axis * 1.384615).rgb * 0.316216;
    sum += inputTexture.Sample(inputSampler, input.uv + axis * 3.230769).rgb * 0.070270;
    sum += inputTexture.Sample(inputSampler, input.uv - axis * 3.230769).rgb * 0.070270;
    return float4(sum, 1.0);
}
