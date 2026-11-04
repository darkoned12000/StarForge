// Depth ordering test: renders a quad at a given Z depth with a given color.
struct PushConstants
{
    float4 mvpRow0;
    float4 mvpRow1;
    float4 mvpRow2;
    float4 mvpRow3;
    float4 colorAndDepth; // rgb = color, a = depth (NDC z)
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    // Fullscreen-ish quad at given depth.
    float2 positions[6] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2( 0.5,  0.5),
        float2( 0.5,  0.5),
        float2(-0.5,  0.5),
        float2(-0.5, -0.5),
    };

    float4x4 mvp = float4x4(pc.mvpRow0, pc.mvpRow1, pc.mvpRow2, pc.mvpRow3);

    VSOutput out;
    out.position = mul(mvp, float4(positions[vertexIndex], pc.colorAndDepth.a, 1.0));
    out.color = pc.colorAndDepth.rgb;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
