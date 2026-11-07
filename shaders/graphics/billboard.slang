// Billboard effects: NDC-centered quad (triangle strip), shapes via push
// constant (0 = glow disc, 1 = beam, 2 = ring, 3 = cone). Additive output.
// Push: centerSize(vec4: xy center NDC, z half-size, w aspect) | color |
//       time, intensity, shape(uint), rotationAngle = 48B.

struct PushConstants
{
    float4 centerSize;
    float4 color;
    float time;
    float intensity;
    uint shape;
    float rotationAngle;
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
    // 4-vertex triangle strip quad.
    float2 corner[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };
    float2 c = corner[vertexId];
    float2 offset = c * pc.centerSize.z;
    offset.x /= max(pc.centerSize.w, 0.001); // Aspect correction: no ovals on 16:9.
    if (pc.shape == 1u || pc.shape == 3u)
    {
        // Rotate the quad so the beam/plume axis follows the projected
        // world direction (see rotationAngle).
        float cs = cos(pc.rotationAngle);
        float sn = sin(pc.rotationAngle);
        offset = float2(offset.x * cs - offset.y * sn, offset.x * sn + offset.y * cs);
        offset.x *= 1.6; // Elongate along the effect axis.
    }
    VSOutput out;
    out.position = float4(pc.centerSize.xy + offset, 0.0, 1.0);
    out.uv = c;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;
    // Unrotate into effect-local space so beam/cone axes read correctly.
    float cs = cos(pc.rotationAngle);
    float sn = sin(pc.rotationAngle);
    float2 luv = pc.shape == 1u || pc.shape == 3u
        ? float2(uv.x * cs + uv.y * sn, -uv.x * sn + uv.y * cs)
        : uv;

    float r = length(luv);
    float flicker = 0.85 + 0.15 * sin(pc.time * 40.0 + luv.x * 20.0);

    float shapeVal = 0.0;
    if (pc.shape == 1u)
    {
        // Beam: thin core along the effect axis (local +x).
        shapeVal = smoothstep(1.0, 0.0, abs(luv.y) * 6.0) * smoothstep(1.0, 0.2, abs(luv.x));
    }
    else if (pc.shape == 2u)
    {
        shapeVal = smoothstep(0.15, 0.0, abs(r - 0.7));
    }
    else if (pc.shape == 3u)
    {
        // Directed cone: radial falloff with brightness along +x (plume tip).
        float axial = smoothstep(-1.0, 1.0, luv.x) * 0.5 + 0.5;
        shapeVal = smoothstep(1.0, 0.0, r) * (0.4 + 0.6 * axial) * flicker;
    }
    else
    {
        shapeVal = smoothstep(1.0, 0.0, r) + smoothstep(0.35, 0.0, r) * 2.0;
    }

    float3 col = pc.color.rgb * shapeVal * pc.intensity;
    return float4(col, 0.0); // Additive (ONE, ONE).
}
