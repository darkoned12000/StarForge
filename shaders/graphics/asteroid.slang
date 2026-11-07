// Instanced asteroid renderer: displaced-rock mesh via BDA, per-instance
// transform (position/scale + unit-quaternion orientation), lambert
// lighting, depth. Hardware indexed drawing supplies the vertex index,
// so the shader reads vertices directly (no second dereference).
// Push: viewProj | vertexAddress, instanceAddress | time = 96B.

struct InstanceData
{
    float4 posScale; // xyz: position, w: uniform scale
    float4 rotation; // xyzw: unit quaternion orientation
};

struct PushConstants
{
    float4x4 viewProj;
    uint64_t vertexBufferAddress;   // BDA: float4 pos / float4 norm (32B stride)
    uint64_t instanceBufferAddress; // BDA: InstanceData[] (32B stride)
    float time;                     // reserved (per-instance rotation is baked)
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<float4> Float4BufferPtr;
typedef vk::BufferPointer<InstanceData> InstanceBufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
};

// Rotate vector v by unit quaternion q.
float3 rotateVector(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Float4BufferPtr pp = { pc.vertexBufferAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexBufferAddress + uint64_t(vertexId) * 32u + 16u };
    float3 localPos = pp.Get().xyz;
    float3 localNormal = np.Get().xyz;

    InstanceBufferPtr ip = { pc.instanceBufferAddress + uint64_t(instanceId) * 32u };
    InstanceData inst = ip.Get();

    float3 scaledPos = localPos * inst.posScale.w;
    float3 worldPos = rotateVector(inst.rotation, scaledPos) + inst.posScale.xyz;
    float3 worldNormal = rotateVector(inst.rotation, localNormal);

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    out.normal = worldNormal;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 lightDir = normalize(float3(1.0, 0.5, 0.3));
    float3 n = normalize(input.normal);
    float diffuse = max(dot(n, lightDir), 0.0);
    float3 base = float3(0.42, 0.40, 0.38);
    float3 ambient = base * 0.15;
    float3 color = ambient + base * diffuse;
    return float4(color, 1.0);
}
