// Planet patch renderer (M7.2b): draws one quadtree patch mesh read via
// buffer device address. Vertex positions are relative-to-center (RTC)
// offsets; `viewProj` is built relative to the patch center on the CPU
// (camera position minus patch center, computed in double), so the
// shader only ever sees small single-precision offsets.
//
// Hardware indexed drawing (vkCmdBindIndexBuffer + vkCmdDrawIndexed):
// SV_VertexID is the dereferenced index, so the GPU post-transform
// cache reuses transformed vertices across shared triangle edges.

struct PlanetVertex
{
    float3 rtcPos;
    float3 normal;
    float2 uv;
}; // 32 bytes (HLSL packing: 0, 12, 24)

struct PushConstants
{
    float4x4 viewProj;
    uint64_t vertexBufferAddress; // BDA: PlanetVertex[]
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<PlanetVertex> VertexBufferPtr;

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
    VertexBufferPtr vp = { pc.vertexBufferAddress + uint64_t(vertexId) * 32u };
    PlanetVertex v = vp.Get();

    VSOutput out;
    out.position = mul(pc.viewProj, float4(v.rtcPos, 1.0));
    out.normal = v.normal;
    out.uv = v.uv;
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
