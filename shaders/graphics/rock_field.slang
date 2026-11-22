// M15.0.8 instanced rock mesh with toon shading, object-space crater detail
// and screen-space bump. One vkCmdDrawIndexed over instance storage.
#include "output_shoulder.slang"
#include "rock_shape.slang"

struct InstanceData
{
    float4 posScale;     // xyz camera-relative position, w uniform scale
    float4 rotation;     // xyzw unit quaternion
    float4 tintAndWidth; // rgb family tint, a = outline width (world units)
    uint seed;           // stable per-rock id
    float fade;          // 1 = solid, 0 = fully dissolved
    float2 pad;
};
struct Push
{
    float4x4 viewProj;       // 0
    uint64_t vertexAddress;  // 64
    uint64_t instanceAddress;// 72
    float2 projectionScale;  // 80
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
    float3 localPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    uint seed : TEXCOORD2;
    float cavity : TEXCOORD3;
    float fade : TEXCOORD4;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Float4BufferPtr pp = { pc.vertexAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexAddress + uint64_t(vertexId) * 32u + 16u };
    float3 localPos = pp.Get().xyz;
    float3 localNormal = np.Get().xyz;
    // Baked crater cavity in normal.w: 0 = exposed, 1 = deep in a pit.
    float cavity = np.Get().w;

    InstanceBufferPtr ip = { pc.instanceAddress + uint64_t(instanceId) * 64u };
    InstanceData inst = ip.Get();

    // Per-instance non-uniform squash. Asteroids are never round, and the
    // squash is what gives each rock a distinct silhouette from the shared
    // base mesh.
    float sx = 0.72 + 0.56 * rockHash1(float(inst.seed) + 0.7);
    float sy = 0.72 + 0.56 * rockHash1(float(inst.seed) + 1.9);
    float sz = 0.72 + 0.56 * rockHash1(float(inst.seed) + 3.3);
    float3 scale3 = inst.posScale.w * float3(sx, sy, sz);

    // Smooth per-instance shape offset (see rock_shape.slang).
    float disp = rockDisplace(localPos, inst.seed);

    float3 scaled = localPos * scale3 * disp;
    float3 worldPos = rockRotate(inst.rotation, scaled) + inst.posScale.xyz;

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    // The base mesh carries real crater/relief normals, so rotate those
    // instead of the old smooth sphere normal. The per-instance squash is a
    // non-uniform scale, so the normal has to be divided by it (inverse
    // transpose) before it is rotated, otherwise the lighting drifts away
    // from the geometry and the relief flattens out.
    out.normal = rockRotate(inst.rotation, localNormal / scale3);
    out.tint = inst.tintAndWidth.rgb;
    out.localPos = localPos;
    out.worldPos = worldPos;
    out.seed = inst.seed;
    out.cavity = cavity;
    out.fade = inst.fade;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Dissolve the rock out at the edge of the streamed field so it fades
    // away instead of popping into or out of existence.
    if (input.fade < 0.999f)
    {
        if (rockBayer4(input.position.xy) > input.fade) discard;
    }

    float3 n = normalize(input.normal);

    // Object-space surface detail: fine grain for the bump, plus a
    // mid-frequency mottle that varies the albedo so the rock does not read
    // as one flat colour.
    float3 gp = input.localPos * 8.5f + float(input.seed) * 0.23f;
    float grit = rockNoise3(gp);
    float3 mp = input.localPos * 3.1f + float(input.seed) * 0.11f;
    float mottle = rockNoise3(mp);

    // Screen-space bump from the height field: no tangents / UVs needed.
    float3 dpx = ddx(input.worldPos), dpy = ddy(input.worldPos);
    float dhx = ddx(grit), dhy = ddy(grit);
    float3 r1 = cross(dpy, n), r2 = cross(n, dpx);
    float det = dot(dpx, r1);
    if (abs(det) > 1e-8f)
        n = normalize(abs(det) * n - 0.30f * sign(det) * (dhx * r1 + dhy * r2));

    float3 L = normalize(pc.sunDirection);
    float ndl = dot(n, L);

    // Two lighting models are combined: a cel-shaded band set that matches
    // the ship, and a continuous wrap-lit term. The cel bands alone made
    // every rock a flat silhouette - the crater relief was invisible because
    // the whole lit hemisphere fell into a single band - while pure
    // continuous shading lost the art style. Mixing them keeps the bands
    // readable while letting the carved surface show through.
    float wrapped = saturate((ndl + 0.12) * 1.05);
    float smoothLit = wrapped * wrapped * (3.0 - 2.0 * wrapped);
    float toon = 0.5f * smoothstep(0.02, 0.07, ndl) + 0.5f * smoothstep(0.38, 0.46, ndl);
    float lit = lerp(toon, smoothLit, 0.55);
    float shade = 0.15f + 0.88f * lit;

    // The instance tint carries the rock family (chosen on the CPU from the
    // stable per-rock seed), so the gameplay layer owns the palette and the
    // field reads as a varied debris cloud rather than one uniform brown.
    // Per-rock value jitter keeps neighbouring rocks from matching exactly.
    float jitter = 0.78f + 0.34f * rockHash1(float(input.seed) * 1.71f + 4.3f);
    float3 base = input.tint * jitter;

    // Baked cavity: crater floors are occluded (dark), the raised lips catch
    // the light (bright). This is what gives the surface its carved,
    // hand-made look instead of a painted blob.
    float cav = input.cavity; // > 0 inside a pit, < 0 on a raised rim
    float ao = 1.0 - max(cav, 0.0) * 0.80;
    float rimLight = max(-cav, 0.0) * 0.85;

    // Mid-frequency albedo mottle keeps large flat faces from reading as
    // one flat colour.
    float3 color = base * shade * ao * (0.85f + 0.30f * mottle);
    // Raised crater lips get a warm kick where the sun actually reaches
    // them, which is what makes the pits read as carved rather than dirty.
    color += base * rimLight * saturate(ndl * 0.5 + 0.5) * 0.55;

    // Cool rim on the lit side only, matching the ship/planet lighting.
    float3 V = normalize(-input.worldPos);
    float rim = pow(1.0f - saturate(dot(n, V)), 3.0f) * smoothstep(-0.15f, 0.35f, ndl);
    color += float3(0.35f, 0.55f, 0.85f) * rim * 0.30f;

    return float4(outputColor(color), 1.0f);
}
