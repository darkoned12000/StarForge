// M5 Phase 3: indirect cube renderer driven by GPU-culling output.
// gl_InstanceID indexes the compacted visible-ID list (BDA); the original ID
// indexes the SoA bounds buffer (center XYZ + radius W) to place each cube.

struct PushConstants
{
    float4x4 viewProj;
    uint64_t boundsAddress;
    uint64_t visibleIndicesAddress;
};

[[vk::push_constant]]
PushConstants pc;

typedef vk::BufferPointer<float4> BoundsPtr;
typedef vk::BufferPointer<uint> IndexPtr;

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
    IndexPtr indexPtr = { pc.visibleIndicesAddress + uint64_t(instanceIndex) * 4u };
    uint originalId = indexPtr.Get();

    BoundsPtr boundsPtr = { pc.boundsAddress + uint64_t(originalId) * 16u };
    float4 sphere = boundsPtr.Get(); // center XYZ + radius W

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
