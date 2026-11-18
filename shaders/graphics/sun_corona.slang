// Sun Corona Shader (M15.0.7): Additive solar atmosphere shell (5-10x solar radius)
// Renders filaments/prominences and smooth radial falloff around the star.
// Depth testing ensures corona fragments behind the opaque solar disk are occluded.

struct PushConstants
{
    float4x4 viewProj;            // 64 bytes: camera projection * view
    float4 patchOrigin;           // 16 bytes: xyz = camera-relative sun center, w = sun core radius
    float4 coronaColor;           // 16 bytes: RGBA outer corona glow color
    uint64_t vertexBufferAddress; // 8 bytes: BDA pointer to vertex buffer
    float coronaRadius;           // 4 bytes: outer radius of corona shell
    float time;                   // 4 bytes: animation time for prominences
    float intensity;              // 4 bytes: brightness multiplier
    float pad;                    // 4 bytes: alignment to 16 bytes
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<float4> Float4BufferPtr;
typedef vk::BufferPointer<float2> Float2BufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 camRelPos : POSITION1;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    Float4BufferPtr pp = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u };
    Float4BufferPtr np = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u + 16u };
    Float2BufferPtr up = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u + 32u };

    float4 rtcPos = pp.Get();
    float4 rtcNormal = np.Get();

    VSOutput out;
    float3 camRel = rtcPos.xyz + pc.patchOrigin.xyz;
    out.position = mul(pc.viewProj, float4(camRel, 1.0));
    out.normal = rtcNormal.xyz;
    out.camRelPos = camRel;
    out.uv = up.Get();
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Ray from camera (0,0,0) through the corona fragment
    float3 rayDir = normalize(input.camRelPos);
    float3 sunCenter = pc.patchOrigin.xyz;
    float coreRadius = pc.patchOrigin.w;
    float coronaRadius = pc.coronaRadius;

    // Closest approach along the ray
    float t = dot(sunCenter, rayDir);
    if (t <= 0.0)
    {
        discard;
    }

    // Perpendicular offset vector from sun center to view ray
    float3 closestPt = t * rayDir;
    float3 toRay = closestPt - sunCenter;
    float dPerp = length(toRay);

    // Normalized radial distance from core radius outward: 0 at surface, 1 at outer corona
    float s = saturate((dPerp - coreRadius) / max(1.0, coronaRadius - coreRadius));

    // Exponential glow falloff, feathered to exact zero before the mesh edge
    // via a smooth hermite window so no "glass bubble" silhouette remains.
    float glow = (exp(-4.0 * s) * 0.8 + pow(1.0 - s, 4.0) * 0.4)
               * smoothstep(1.0, 0.7, s);

    // Coronal streamers (prominence filaments), softened for majestic flares
    float3 viewAxis = normalize(sunCenter);
    float3 perpDir = normalize(toRay + viewAxis * 0.001);
    float3 up = abs(viewAxis.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(viewAxis, up));
    float3 realUp = cross(right, viewAxis);
    float phi = atan2(dot(perpDir, realUp), dot(perpDir, right));

    float streamers = 0.90
                    + 0.07 * sin(phi * 16.0 + pc.time * 0.6)
                    + 0.03 * sin(phi * 32.0 - pc.time * 1.1);

    float alpha = glow * streamers * pc.intensity;
    float3 color = pc.coronaColor.rgb * alpha;

    return float4(color, alpha);
}
