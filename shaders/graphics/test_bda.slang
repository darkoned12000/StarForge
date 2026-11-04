// Minimal BDA test: read a single uint32 from a buffer at a device address.
struct Vertex
{
    float3 position;
    float3 color;
};

struct PushConstants
{
    float4x4 mvp;
    uint64_t dataAddress;
    uint     expectedValue;
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<uint> UintBufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    UintBufferPtr buf = { pc.dataAddress };
    uint val = buf.Get()[0];

    float3 color = (val == pc.expectedValue) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);

    float3 positions[3] = {
        float3(-0.5, -0.5, 0.0),
        float3( 0.5, -0.5, 0.0),
        float3( 0.0,  0.5, 0.0),
    };

    VSOutput out;
    out.position = mul(pc.mvp, float4(positions[vertexIndex], 1.0));
    out.color = color;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
