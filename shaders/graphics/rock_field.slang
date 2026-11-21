// M15.0.8 instanced rock mesh with toon shading, object-space crater detail
// and screen-space bump. One vkCmdDrawIndexed over instance storage.
#include "output_shoulder.slang"

struct InstanceData
{
    float4 posScale;     // xyz camera-relative position, w uniform scale
    float4 rotation;     // xyzw unit quaternion
    float4 tintAndWidth; // rgb family tint, a = outline width (world units)
    uint seed;           // stable per-rock id
    float3 pad;
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
};

float3 rotateVector(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
float hash1(float x) { return fract(sin(x * 127.1) * 43758.5453); }

// Cheap 3D value noise for craters / surface variation (no UVs required).
float rockHash(float3 p)
{
    return fract(sin(dot(p, float3(127.1f, 311.7f, 74.7f))) * 43758.5453f);
}
float rockNoise(float3 p)
{
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(rockHash(i), rockHash(i + float3(1,0,0)), f.x),
                     lerp(rockHash(i + float3(0,1,0)), rockHash(i + float3(1,1,0)), f.x), f.y),
                lerp(lerp(rockHash(i + float3(0,0,1)), rockHash(i + float3(1,0,1)), f.x),
                     lerp(rockHash(i + float3(0,1,1)), rockHash(i + float3(1,1,1)), f.x), f.y), f.z);
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

    float sx = 0.75 + 0.5 * hash1(float(inst.seed) + 0.7);
    float sy = 0.75 + 0.5 * hash1(float(inst.seed) + 1.9);
    float sz = 0.75 + 0.5 * hash1(float(inst.seed) + 3.3);
    float3 scale3 = inst.posScale.w * float3(sx, sy, sz);

    float dirKey = dot(localNormal, float3(11.3, 7.7, 19.1)) + float(inst.seed) * 0.61;
    float disp = 1.0 + (hash1(dirKey) - 0.5) * 0.12;

    float3 scaled = localPos * scale3 * disp;
    float3 worldPos = rotateVector(inst.rotation, scaled) + inst.posScale.xyz;

    VSOutput out;
    out.position = mul(pc.viewProj, float4(worldPos, 1.0));
    out.normal = rotateVector(inst.rotation, localNormal);
    out.tint = inst.tintAndWidth.rgb;
    out.localPos = localPos;
    out.worldPos = worldPos;
    out.seed = inst.seed;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 n = normalize(input.normal);

    // Object-space craters: smooth basins with raised rims from worley-like
    // cell distances, plus fine noise for grit. Stable per rock via seed.
    float3 p = input.localPos * 2.7f + float(input.seed) * 0.13f;
    float h = rockNoise(p) * 0.55f + rockNoise(p * 2.3f) * 0.25f;

    // Screen-space bump from the height field: no tangents / UVs needed.
    float3 dpx = ddx(input.worldPos), dpy = ddy(input.worldPos);
    float dhx = ddx(h), dhy = ddy(h);
    float3 r1 = cross(dpy, n), r2 = cross(n, dpx);
    float det = dot(dpx, r1);
    if (abs(det) > 1e-8f)
        n = normalize(abs(det) * n - 0.45f * sign(det) * (dhx * r1 + dhy * r2));

    float3 L = normalize(pc.sunDirection);
    float ndl = dot(n, L);
    // Match the ship's three toon bands so rocks read as the same art style.
    float lit = 0.5f * smoothstep(0.02f, 0.06f, ndl) + 0.5f * smoothstep(0.42f, 0.48f, ndl);
    float shade = 0.16f + 0.92f * lit;

    // One rock family: constrain tint toward a warm slate palette with only
    // small per-instance variation instead of unrelated green/steel hues.
    float family = hash1(float(input.seed) * 0.37f);
    float3 slate = lerp(float3(0.38f, 0.33f, 0.29f), float3(0.52f, 0.44f, 0.36f), family);
    float3 base = lerp(slate, input.tint, 0.25f);
    float3 color = base * shade * (0.78f + 0.5f * h);

    // Cool rim on the lit side only, matching the ship/planet lighting.
    float3 V = normalize(-input.worldPos);
    float rim = pow(1.0f - saturate(dot(n, V)), 3.0f) * smoothstep(-0.15f, 0.35f, ndl);
    color += float3(0.35f, 0.55f, 0.85f) * rim * 0.35f;

    return float4(outputColor(color), 1.0f);
}
