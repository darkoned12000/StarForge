// Wormhole look: swirling spiral tunnel around the projected center.
// Push: color(vec4) | center(vec2) | resolution | radius, time,
//       intensity, spin = 48B.

struct PushConstants
{
    float4 color;
    float2 center;      // Vortex center in [0,1] UV space
    float2 resolution;
    float radius;       // Vortex radius in UV units
    float time;
    float intensity;
    float spin;
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
    float2 p = float2(input.uv.x * aspect - pc.center.x * aspect, input.uv.y - pc.center.y);
    float r = length(p) / max(pc.radius, 1e-4);
    float ang = atan2(p.y, p.x);

    float swirl = ang + pc.time * pc.spin * 2.0 + (1.0 / (r + 0.1)) * pc.spin;
    float bands = sin(swirl * 6.0) * 0.5 + 0.5;
    float tunnel = exp(-r * 2.5);
    float core = smoothstep(0.08, 0.0, r);

    float3 col = pc.color.rgb * bands * tunnel * pc.intensity
               + pc.color.bgr * core * pc.intensity;
    return float4(col, saturate(bands * tunnel + core));
}
