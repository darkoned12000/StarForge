// M15.0.4 atmosphere limb. A fullscreen triangle computes the ray's closest
// approach to the planet; a Fresnel-weighted exponential glow is added around
// the limb (outside the opaque disc). Projective depth is written so nearer
// objects occlude the halo, but depth writes are disabled by the pipeline.

struct Push
{
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 cameraPos;    // xyz = camera position (camera-relative)
    float4 planet;       // xyz = centre, w = planet radius
    float4 atmoColor;    // rgb = glow colour, a = intensity
    float4 params;       // x = atmosphere radius ratio, y = falloff, z = depth bias
};
[[vk::push_constant]] Push pc;

struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    float2 pos = float2((vertexIndex == 1u) ? 3.0 : -1.0, (vertexIndex == 2u) ? 3.0 : -1.0);
    VSOutput out;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

float3 rayDirection(float2 uv)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 nearP = mul(pc.invViewProj, float4(ndc, 0.0, 1.0));
    float4 farP = mul(pc.invViewProj, float4(ndc, 1.0, 1.0));
    return normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, out float depth : SV_Depth) : SV_Target
{
    float3 ro = pc.cameraPos.xyz;
    float3 rd = rayDirection(input.uv);
    float3 centre = pc.planet.xyz;
    float radius = pc.planet.w;
    float atmoRadius = radius * pc.params.x;

    float3 oc = ro - centre;
    float b = dot(oc, rd);
    // Perpendicular distance from centre to the ray.
    float closest = sqrt(max(dot(oc, oc) - b * b, 0.0));
    if (closest >= atmoRadius || closest < radius * 0.92) discard;

    // Glow peaks just above the surface and falls off toward the shell edge.
    float t = saturate((closest - radius) / max(atmoRadius - radius, 1e-3));
    float glow = pow(1.0 - t, pc.params.y) * pc.atmoColor.a;

    // Fresnel-like limb emphasis.
    float3 shellPoint = ro + rd * max(b, 0.0);
    float3 N = normalize(shellPoint - centre);
    float3 V = normalize(ro - shellPoint);
    float fres = pow(1.0 - abs(dot(N, V)), 2.0);
    glow *= (0.35 + 0.65 * fres);

    // Depth at the near atmosphere-shell intersection (biased slightly back).
    float discOuter = b * b - (dot(oc, oc) - atmoRadius * atmoRadius);
    float tEnter = b - sqrt(max(discOuter, 0.0));
    float3 depthPoint = ro + rd * max(tEnter, 0.0);
    float4 clip = mul(pc.viewProj, float4(depthPoint, 1.0));
    depth = saturate(clip.z / clip.w) + pc.params.z;

    return float4(pc.atmoColor.rgb * glow, glow);
}
