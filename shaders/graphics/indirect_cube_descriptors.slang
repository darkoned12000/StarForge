// M5 Phase 4 fallback: same indirect cube draw as indirect_cube.slang, but
// instance data is read through descriptor set 0 instead of BDA when
// GpuCapabilities.bindless is unavailable.

struct PushConstants
{
    float4x4 viewProj;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
StructuredBuffer<float4> boundsBuffer;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> visibleIndices;

static const float3 kCubePositions[36] = {
    float3(-0.5,-0.5, 0.5), float3(0.5,-0.5,0.5), float3(0.5,0.5,0.5),
    float3(0.5,0.5,0.5), float3(-0.5,0.5,0.5), float3(-0.5,-0.5,0.5),
    float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5), float3(-0.5,0.5,-0.5),
    float3(-0.5,0.5,-0.5), float3(0.5,0.5,-0.5), float3(0.5,-0.5,-0.5),
    float3(-0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5), float3(-0.5,0.5,0.5),
    float3(-0.5,0.5,0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,-0.5),
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(0.5,0.5,0.5), float3(0.5,-0.5,0.5),
    float3(-0.5,0.5,0.5), float3(0.5,0.5,0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,0.5),
    float3(-0.5,-0.5,-0.5), float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5),
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5),
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    uint originalId = visibleIndices[instanceIndex];
    float4 sphere = boundsBuffer[originalId];

    float3 local = kCubePositions[vertexIndex % 36u];
    float3 world = local * max(sphere.w, 0.001f) + sphere.xyz;

    VSOutput out;
    out.position = mul(pc.viewProj, float4(world, 1.0f));
    out.normal = local;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(0.25f + abs(input.normal.x) * 0.5f,
                  0.6f + abs(input.normal.y) * 0.3f,
                  0.9f, 1.0f);
}
