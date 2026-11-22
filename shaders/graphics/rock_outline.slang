// M15.0.9 distance-aware inverted-hull outline for instanced rocks. The
// extrusion width is computed per instance on the CPU (from camera distance,
// resolution and scale) and stored in tintAndWidth.a, so no extra push data
// is needed. Front faces are culled by the pipeline, leaving only the rim.
//
// The shape maths is shared with rock_field.slang (via rock_shape.slang) so
// the ink outline traces exactly the silhouette of the rock it belongs to.
#include "rock_shape.slang"

struct InstanceData
{
    float4 posScale;
    float4 rotation;
    float4 tintAndWidth;
    uint seed;
    float fade;
    float2 pad;
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

struct VSOutput { float4 position : SV_Position; float3 tint : COLOR0; float fade : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Float4BufferPtr pp = { pc.vertexAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexAddress + uint64_t(vertexId) * 32u + 16u };
    float3 localPos = pp.Get().xyz;
    InstanceBufferPtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();

    // Same per-instance squash and displacement as rock_field.slang.
    float sx = 0.72 + 0.56 * rockHash1(float(inst.seed) + 0.7);
    float sy = 0.72 + 0.56 * rockHash1(float(inst.seed) + 1.9);
    float sz = 0.72 + 0.56 * rockHash1(float(inst.seed) + 3.3);
    float3 scale3 = inst.posScale.w * float3(sx, sy, sz);
    float disp = rockDisplace(localPos, inst.seed);

    float3 scaled = localPos * scale3 * disp;
    float3 worldPos = rockRotate(inst.rotation, scaled) + inst.posScale.xyz;
    // Extrude along the smooth radial direction, not the crater relief
    // normal: an inverted hull built from high-frequency normals tears
    // open at every crater rim.
    float3 radial = normalize(scaled);
    worldPos += rockRotate(inst.rotation, radial) * inst.tintAndWidth.a;

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    out.tint = inst.tintAndWidth.rgb;
    out.fade = inst.fade;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Match the mesh's dissolve so the ink fades out with the rock.
    if (input.fade < 0.999f)
    {
        if (rockBayer4(input.position.xy) > input.fade) discard;
    }
    float3 ink = input.tint * 0.08 + float3(0.02, 0.02, 0.04);
    return float4(ink, 1.0);
}
