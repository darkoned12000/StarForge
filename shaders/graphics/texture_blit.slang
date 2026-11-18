// Fullscreen pass with a sampled input image. Reusable primitive for
// HDR tonemap, bloom and screen-space post effects.
[[vk::binding(0, 0)]]
Texture2D<float4> inputTexture;
[[vk::binding(1, 0)]]
SamplerState inputSampler;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

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
    return inputTexture.Sample(inputSampler, input.uv);
}
