// Simple test: output color based on vertex index (no BDA, no push constants needed).
struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    float3 positions[3] = {
        float3(-0.5, -0.5, 0.0),
        float3( 0.5, -0.5, 0.0),
        float3( 0.0,  0.5, 0.0),
    };
    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0),
    };

    VSOutput out;
    out.position = float4(positions[vertexIndex], 1.0);
    out.color = colors[vertexIndex];
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
