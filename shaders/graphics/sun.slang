// Sun Core Shader (M15.0.7): Emissive celestial body with Eddington limb darkening
// I(μ) = 1 - u(1 - μ), where μ = max(0, n · v).
// Vertices are provided via Buffer Device Address (BDA) using 48-byte stride
// (float4 position RTC, float4 normal, float2 uv).
// Camera-relative patch origin ensures jitter-free rendering at astronomical scale.

struct PushConstants
{
    float4x4 viewProj;            // 64 bytes: camera projection * view
    float4 patchOrigin;           // 16 bytes: xyz = camera-relative origin of patch/body, w = unused
    float4 coreColor;             // 16 bytes: RGBA emission color at disk center
    float4 limbColor;             // 16 bytes: RGBA emission color at disk edge
    uint64_t vertexBufferAddress; // 8 bytes: BDA pointer to vertex buffer
    float limbDarkeningCoeff;     // 4 bytes: u in 1 - u*(1 - μ) (standard solar value ~0.6)
    float time;                   // 4 bytes: animation time for solar granulation
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
    float3 n = normalize(input.normal);
    // In camera-relative space, camera is at (0, 0, 0)
    float3 v = normalize(-input.camRelPos);
    float mu = max(dot(n, v), 0.0);

    // Linear Eddington limb darkening: I(μ) = 1.0 - u * (1.0 - μ)
    float u = clamp(pc.limbDarkeningCoeff, 0.0, 1.0);
    float limbDarkening = 1.0 - u * (1.0 - mu);

    // Color ramp: disk center approaches coreColor, rim approaches limbColor
    float t = pow(mu, 0.7);
    float3 baseColor = lerp(pc.limbColor.rgb, pc.coreColor.rgb, t);

    // Solar granulation: subtle dynamic convection cells
    float cell1 = sin(input.uv.x * 120.0 + pc.time * 0.4) * cos(input.uv.y * 120.0 - pc.time * 0.3);
    float cell2 = sin(input.uv.x * 240.0 - pc.time * 0.7) * cos(input.uv.y * 240.0 + pc.time * 0.5);
    float granulation = 0.95 + 0.05 * (cell1 + cell2 * 0.5);

    float3 finalColor = baseColor * limbDarkening * granulation;
    return float4(finalColor, 1.0);
}
