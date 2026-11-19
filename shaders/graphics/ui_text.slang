// M16.2 UI text: one quad per lit glyph pixel, vertex data via BDA.
// CPU supplies clip-space corners + colour, so no projection math here.
struct Vertex
{
    float4 position;
    float4 color;
};
struct Push
{
    uint64_t vertexAddress;
};
[[vk::push_constant]] Push pc;
typedef vk::BufferPointer<Vertex> VertexPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    VertexPtr vp = { pc.vertexAddress + uint64_t(vertexId) * 32u };
    Vertex v = vp.Get();
    VSOutput out;
    out.position = v.position;
    out.color = v.color;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return input.color;
}
