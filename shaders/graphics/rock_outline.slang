// M15.0.9 distance-aware inverted-hull outline for instanced rocks. The
// extrusion width is computed per instance on the CPU (from camera distance,
// resolution and scale) and stored in tintAndWidth.a, so no extra push data
// is needed. Front faces are culled by the pipeline, leaving only the rim.
struct InstanceData
{
    float4 posScale;
    float4 rotation;
    float4 tintAndWidth;
    uint seed;
    float3 pad;
};
struct Push
{
    float4x4 viewProj;
    uint64_t vertexAddress;
    uint64_t instanceAddress;
    float2 projectionScale;
    float2 resolution;
    float3 sunDirection;
    float pad;
};
[[vk::push_constant]] Push pc;
typedef vk::BufferPointer<float4> Float4BufferPtr;
typedef vk::BufferPointer<InstanceData> InstanceBufferPtr;

struct VSOutput { float4 position : SV_Position; float3 tint : COLOR0; };

float3 rotateVector(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Float4BufferPtr pp = { pc.vertexAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexAddress + uint64_t(vertexId) * 32u + 16u };
    float3 localPos = pp.Get().xyz;
    float3 localNormal = np.Get().xyz;
    InstanceBufferPtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();

    float3 worldNormal = rotateVector(inst.rotation, localNormal);
    float3 scaled = localPos * inst.posScale.w;
    float3 worldPos = rotateVector(inst.rotation, scaled) + inst.posScale.xyz;
    // Extrude along the normal in world units (width already distance-aware).
    worldPos += worldNormal * inst.tintAndWidth.a;

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    out.tint = inst.tintAndWidth.rgb;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 ink = input.tint * 0.08 + float3(0.02, 0.02, 0.04);
    return float4(ink, 1.0);
}
