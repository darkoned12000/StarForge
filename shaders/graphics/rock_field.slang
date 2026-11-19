// M15.0.8 instanced rock mesh: per-instance tinted palette, per-instance
// procedural shape variation (non-uniform scale + directional displacement)
// over a shared lumpy base mesh. One vkCmdDrawIndexed over instance storage.
struct InstanceData
{
    float4 posScale;     // xyz camera-relative position, w uniform scale
    float4 rotation;     // xyzw unit quaternion
    float4 tintAndWidth; // rgb palette tint, a = outline width (world units)
    uint seed;           // stable per-rock id (shape variety, never frame index)
    float3 pad;
};
struct Push
{
    float4x4 viewProj;       // 0
    uint64_t vertexAddress;  // 64  (float4 pos / float4 normal, 32B stride)
    uint64_t instanceAddress;// 72  (InstanceData[], 48B stride)
    float2 projectionScale;  // 80  proj[0][0], proj[1][1]
    float2 resolution;       // 88
    float3 sunDirection;     // 96
    float pad;               // 108
};
[[vk::push_constant]] Push pc;
typedef vk::BufferPointer<float4> Float4BufferPtr;
typedef vk::BufferPointer<InstanceData> InstanceBufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 tint : COLOR0;
};

float3 rotateVector(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
float hash1(float x) { return fract(sin(x * 127.1) * 43758.5453); }

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Float4BufferPtr pp = { pc.vertexAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexAddress + uint64_t(vertexId) * 32u + 16u };
    float3 localPos = pp.Get().xyz;
    float3 localNormal = np.Get().xyz;

    InstanceBufferPtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();

    // Per-instance shape variety keyed by *stable* seed, not frame index.
    float sx = 0.75 + 0.5 * hash1(float(inst.seed) + 0.7);
    float sy = 0.75 + 0.5 * hash1(float(inst.seed) + 1.9);
    float sz = 0.75 + 0.5 * hash1(float(inst.seed) + 3.3);
    float3 scale3 = inst.posScale.w * float3(sx, sy, sz);

    // Small coherent directional displacement keyed by instance + direction.
    float dirKey = dot(localNormal, float3(11.3, 7.7, 19.1)) + float(inst.seed) * 0.61;
    float disp = 1.0 + (hash1(dirKey) - 0.5) * 0.12;

    float3 scaled = localPos * scale3 * disp;
    float3 worldPos = rotateVector(inst.rotation, scaled) + inst.posScale.xyz;
    float3 worldNormal = rotateVector(inst.rotation, localNormal);

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    out.normal = worldNormal;
    out.tint = inst.tintAndWidth.rgb;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 n = normalize(input.normal);
    float diffuse = max(dot(n, normalize(pc.sunDirection)), 0.0);
    // Toon bands with a low floor: faces pointing away from the sun stay
    // dark silhouettes when the sun is behind the rock.
    float lit = diffuse > 0.5 ? 1.0 : (diffuse > 0.18 ? 0.6 : 0.0);
    float3 color = input.tint * (0.12 + 0.95 * lit);
    return float4(color, 1.0);
}
