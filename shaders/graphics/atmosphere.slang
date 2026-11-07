// Atmosphere shell renderer (M7.4b): draws the atmosphere sphere,
// sampling the precomputed scattering LUT (see AtmosphereLut) via BDA.
// LUT parametrization: muView = dot(normal, viewDir),
// muSun = dot(normal, sunDir), both in [-1, 1].

struct PushConstants
{
    float4x4 viewProj;
    uint64_t vertexBufferAddress; // BDA: float4 position / float4 normal interleaved, stride 32
    uint64_t lutAddress;          // BDA: float4[] LUT texels (w unused)
    float3 cameraPos;
    float3 sunDir;
    uint lutWidth;
    uint lutHeight;
}; // 116 bytes (float3 members are 16-byte aligned in std430)

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<float4> Float4BufferPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 worldPos : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    // NOTE: this Slang version's vk::BufferPointer has no variable
    // subscript; index via raw address arithmetic + Get() (element).
    Float4BufferPtr pp = { pc.vertexBufferAddress + uint64_t(vertexId) * 32u };
    Float4BufferPtr np = { pc.vertexBufferAddress + uint64_t(vertexId) * 32u + 16u };

    float4 pos = pp.Get();
    float4 normal = np.Get();

    VSOutput out;
    out.position = mul(pc.viewProj, pos);
    out.normal = normal.xyz;
    out.worldPos = pos.xyz;
    return out;
}

float3 fetchLut(uint index)
{
    Float4BufferPtr lp = { pc.lutAddress + uint64_t(index) * 16u };
    return lp.Get().rgb;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 n = normalize(input.normal);
    float3 v = normalize(pc.cameraPos - input.worldPos);
    float3 l = normalize(pc.sunDir);

    float2 mu = float2(dot(n, v), dot(n, l));
    float2 grid = (mu * 0.5 + 0.5) * float2(float(pc.lutWidth), float(pc.lutHeight)) - 0.5;
    grid = clamp(grid, float2(0.0, 0.0), float2(float(pc.lutWidth - 1), float(pc.lutHeight - 1)));

    uint x0 = uint(grid.x);
    uint y0 = uint(grid.y);
    uint x1 = min(x0 + 1u, pc.lutWidth - 1u);
    uint y1 = min(y0 + 1u, pc.lutHeight - 1u);
    float2 f = frac(grid);

    float3 c00 = fetchLut(y0 * pc.lutWidth + x0);
    float3 c10 = fetchLut(y0 * pc.lutWidth + x1);
    float3 c01 = fetchLut(y1 * pc.lutWidth + x0);
    float3 c11 = fetchLut(y1 * pc.lutWidth + x1);

    float3 color = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
    return float4(color, 1.0);
}
