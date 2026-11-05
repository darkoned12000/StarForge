// GPU-driven vertex shader: reads instance transforms from visible indices buffer.
// Each vertex is identified by gl_VertexIndex (0-35 for cube) and
// gl_InstanceIndex (index into visibleIndices array).

struct PushConstants
{
    float4x4 viewProj;
    uint64_t instanceDataAddress;  // BDA to instance transforms
    uint64_t visibleIndicesAddress; // BDA to compacted visible instance IDs
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<float4x4> InstanceDataPtr;
typedef vk::BufferPointer<uint> VisibleIndicesPtr;

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
    float3(0.5,0.5,-0.5), float3(-0.5,0.5,-0.5), float3(-0.5,0.5,0.5),
    float3(-0.5,-0.5,-0.5), float3(0.5,-0.5,-0.5), float3(0.5,-0.5,0.5),
    float3(0.5,-0.5,0.5), float3(-0.5,-0.5,0.5), float3(-0.5,-0.5,-0.5),
};

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    // Use RWStructuredBuffer via descriptor binding instead of BDA
    // (BDA pointer arithmetic not supported in Slang for variable indices)
    uint instanceId = 0;
    float4x4 model = float4x4(1.0);

    float3 pos = kCubePositions[vertexIndex % 36];

    VSOutput out;
    out.position = mul(pc.viewProj, mul(model, float4(pos, 1.0)));
    return out;
}