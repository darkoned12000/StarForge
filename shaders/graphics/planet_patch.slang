// Planet patch renderer (M7.2b): draws one quadtree patch mesh read via
// buffer device address. Vertex positions are relative-to-center (RTC)
// offsets; `viewProj` is built relative to the patch center on the CPU
// (camera position minus patch center, computed in double), so the
// shader only ever sees small single-precision offsets.
//
// Hardware indexed drawing (vkCmdBindIndexBuffer + vkCmdDrawIndexed):
// SV_VertexID is the dereferenced index, so the GPU post-transform
// cache reuses transformed vertices across shared triangle edges.
//
// Vertex format: float4 position / float4 normal / float2 uv interleaved
// (std430-safe stride 48) — float3 structs straddle in storage buffers.

struct PushConstants
{
    float4x4 viewProj;
    uint64_t vertexBufferAddress; // BDA: interleaved vertices (48B stride)
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<float4> Float4BufferPtr;
typedef vk::BufferPointer<float2> Float2BufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    // NOTE: this Slang version's vk::BufferPointer has no variable
    // subscript; index via raw address arithmetic + Get() (element).
    Float4BufferPtr pp = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u };
    Float4BufferPtr np = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u + 16u };
    Float2BufferPtr up = { pc.vertexBufferAddress + uint64_t(vertexId) * 48u + 32u };

    float4 rtcPos = pp.Get();
    float4 rtcNormal = np.Get();

    VSOutput out;
    out.position = mul(pc.viewProj, rtcPos);
    out.normal = rtcNormal.xyz;
    out.uv = up.Get();
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Single fixed light direction (view-independent for now).
    float3 lightDir = normalize(float3(1.0, 0.5, 0.3));
    float3 n = normalize(input.normal);
    float diffuse = max(dot(n, lightDir), 0.0);

    // Terrain base color with subtle UV-based variation.
    float3 base = float3(0.35, 0.5, 0.25) * (0.85 + 0.15 * input.uv.x);
    float3 color = base * (0.35 + 0.65 * diffuse);

    return float4(color, 1.0);
}
