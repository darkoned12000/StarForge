// M15.0.9 cheap distant impostor: one camera-facing quad per rock, drawn with
// a single vkCmdDraw(6, instanceCount). Shaded as a soft tinted disc; depth is
// the instance centre so it occludes consistently with the detailed rocks.
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
typedef vk::BufferPointer<InstanceData> InstanceBufferPtr;

static const float2 kQuad[6] = {
    float2(-1,-1), float2(1,-1), float2(1,1),
    float2(-1,-1), float2(1,1), float2(-1,1),
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 tint : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    InstanceBufferPtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();
    float2 corner = kQuad[min(vertexId, 5u)];

    float4 clip = mul(pc.viewProj, float4(inst.posScale.xyz, 1.0));
    float radius = inst.posScale.w;
    float2 offset = corner * radius * pc.projectionScale / max(clip.w, 1e-3);
    clip.xy += offset;

    VSOutput out;
    out.position = clip;
    out.uv = corner;
    out.tint = inst.tintAndWidth.rgb;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float r = length(input.uv);
    if (r > 1.0) discard;
    float shade = 0.35 + 0.65 * smoothstep(1.0, 0.25, r);
    return float4(input.tint * shade, 1.0);
}
