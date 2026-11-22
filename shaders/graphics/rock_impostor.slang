// M15.0.9 cheap distant impostor: one camera-facing quad per rock, drawn with
// a single vkCmdDraw(6, instanceCount). Shaded as a lit sphere so distant rocks
// keep the same read as the near meshes, and dissolved with a screen-door
// dither near the edge of the streamed field so rocks fade in instead of
// popping into existence.
#include "rock_shape.slang"

struct InstanceData
{
    float4 posScale;
    float4 rotation;
    float4 tintAndWidth;
    uint seed;
    float fade;   // 1 = fully visible, 0 = fully dissolved
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

static const float2 kQuad[6] = {
    float2(-1,-1), float2(1,-1), float2(1,1),
    float2(-1,-1), float2(1,1), float2(-1,1),
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 tint : COLOR0;
    float fade : TEXCOORD1;
    uint seed : TEXCOORD2;
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
    out.fade = inst.fade;
    out.seed = inst.seed;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Lumpy silhouette. A perfect circle reads as a sticker, so the discard
    // radius is modulated by the same kind of coherent noise the near mesh
    // uses - the impostor then has the same family of outlines as the
    // detailed rocks and the LOD swap does not change the shape.
    float ang = atan2(input.uv.y, input.uv.x);
    float3 sp = float3(cos(ang), sin(ang), 0.0) * 2.3 + float(input.seed % 977u) * 0.371;
    float lump = rockNoise3(sp);
    float lump2 = rockNoise3(sp * 2.7 + 11.0);
    float silhouette = 0.80 + 0.15 * lump + 0.09 * lump2;

    float r = length(input.uv);
    if (r > silhouette) discard;

    // Fake a sphere normal from the impostor disc so distant rocks are
    // round-shaded like the near ones, instead of flat stickers.
    float3 n = float3(input.uv, sqrt(max(0.0, 1.0 - r * r)));
    float3 L = normalize(pc.sunDirection);
    float ndl = dot(n, L);
    // Same hybrid cel/continuous light as rock_field.slang, so the LOD
    // swap does not change the shading.
    float wrapped = saturate((ndl + 0.12) * 1.05);
    float smoothLit = wrapped * wrapped * (3.0 - 2.0 * wrapped);
    float toon = 0.5f * smoothstep(0.02, 0.07, ndl) + 0.5f * smoothstep(0.38, 0.46, ndl);
    float lit = lerp(toon, smoothLit, 0.55);
    float shade = 0.15f + 0.88f * lit;

    // Edge darkening gives the impostor a rounded, solid read.
    float edge = smoothstep(1.0, 0.80, r);
    float3 color = input.tint * shade * (0.55 + 0.45 * edge);
    // The dissolve factor doubles as the impostor's opacity: a distant rock
    // fades up out of the background and solidifies as it comes into focus
    // instead of blinking into existence.
    return float4(color, saturate(input.fade));
}
