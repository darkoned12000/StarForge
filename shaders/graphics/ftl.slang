// FTL travel look: radial speed streaks from the velocity focal point.
// Push: color(vec4) | resolution | focalPoint | time,intensity,speed,seed = 48B.

struct PushConstants
{
    float4 color;
    float2 resolution;
    float2 focalPoint; // Vanishing point in [0,1] UV space
    float time;
    float intensity;
    float speed;
    float seed;
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput out;
    float2 pos = positions[vertexId];
    out.position = float4(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

float hash21(float2 p)
{
    uint2 u = uint2(asuint(p.x), asuint(p.y));
    u = u * 1664525u + 1013904223u;
    u.x += u.y * 1664525u;
    u.y += u.x * 1664525u;
    u = u ^ (u >> 16u);
    u.x += u.y * 1664525u;
    return float(u.x) * (1.0 / 4294967296.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    const float PI = 3.14159265359;
    float aspect = pc.resolution.x / max(pc.resolution.y, 1.0);
    float2 p = float2(input.uv.x * aspect - pc.focalPoint.x * aspect, input.uv.y - pc.focalPoint.y);
    float r = length(p);

    // Continuous normalized angle in [0, 1) — no atan2 branch-cut seam.
    float ang = (atan2(p.y, p.x) + PI) / (2.0 * PI);
    float rayId = floor(ang * 240.0);

    // Continuous radial flow per ray (no concentric ring banding).
    float streakSeed = hash21(float2(rayId, pc.seed));
    float speedOffset = pc.time * pc.speed * (1.5 + streakSeed * 2.0);
    float flow = frac(r * 4.0 - speedOffset);
    float streakMask = step(0.75, hash21(float2(rayId, 17.3)));

    float fade = smoothstep(0.04, 0.35, r) * exp(-r * 1.5);
    float streak = streakMask * fade * smoothstep(0.0, 0.8, flow);

    float core = smoothstep(0.08, 0.0, r);

    float3 col = pc.color.rgb * streak * 2.5 * pc.intensity +
                 pc.color.bgr * core * pc.intensity;
    return float4(col, saturate(streak + core));
}
