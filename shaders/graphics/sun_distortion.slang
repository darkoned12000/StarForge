// Sun distortion look: exponential corona glow + thin shimmering ring
// around the sun's screen position. True refraction/lensing needs a
// scene color texture input (future); this is a glow+ring pass.
// Push: time, intensity, sunRadius | sunPos(vec2) | resolution = 32B.

struct PushConstants
{
    float time;
    float intensity;
    float sunRadius;
    float pad0;
    float2 sunPos;     // [0,1] UV space (offset 16)
    float2 resolution; // offset 24
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

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float aspect = pc.resolution.x / max(pc.resolution.y, 1.0);
    float2 p = float2(input.uv.x * aspect, input.uv.y);
    float2 s = float2(pc.sunPos.x * aspect, pc.sunPos.y);
    float d = length(p - s);

    float glow = exp(-d * (4.0 / max(pc.sunRadius, 0.01))) * pc.intensity;

    // True thin ring around the solar perimeter.
    float ang = atan2(p.y - s.y, p.x - s.x);
    float shimmer = 0.85 + 0.15 * sin(ang * 24.0 + pc.time * 3.0);
    float ringWidth = pc.sunRadius * 0.08;
    float ring = smoothstep(ringWidth, 0.0, abs(d - pc.sunRadius)) * shimmer * pc.intensity;

    float3 col = float3(1.0, 0.85, 0.55) * glow + float3(1.0, 0.95, 0.8) * ring;
    return float4(col, saturate(glow + ring));
}
