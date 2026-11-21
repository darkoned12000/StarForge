// Bloom upsample: 3x3 tent filter, ADDED onto the next larger mip.
//
// Walk the chain from the smallest mip upward: sample mip N+1, write into
// mip N with loadOp = LOAD and additive blending (ONE, ONE). The final
// (largest) mip is what tonemap.slang samples as its bloom input.
// Descriptor set 0: binding 0 = source image, binding 1 = linear clamp sampler.
// Push: 16 bytes. texelSize is 1 / (source width, source height).

struct PushConstants
{
    float2 texelSize;
    float radius;    // tent radius in source texels, ~1.0 (larger = softer, wider glow)
    float intensity; // per-level weight; ~0.6-1.0
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Texture2D<float4> srcTex;
[[vk::binding(1, 0)]] SamplerState linearClamp;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput output;
    float2 p = positions[vertexId];
    output.position = float4(p, 0.0, 1.0);
    // Flipped viewport (height < 0): V = 0.5 - 0.5*y keeps output row r
    // reading input row r, matching hdr_bright/hdr_blur/hdr_resolve.
    output.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 d = pc.texelSize * pc.radius;
    float2 uv = input.uv;

    float3 s = srcTex.SampleLevel(linearClamp, uv, 0).rgb * 4.0;
    s += srcTex.SampleLevel(linearClamp, uv + float2(-d.x, 0.0), 0).rgb * 2.0;
    s += srcTex.SampleLevel(linearClamp, uv + float2( d.x, 0.0), 0).rgb * 2.0;
    s += srcTex.SampleLevel(linearClamp, uv + float2(0.0, -d.y), 0).rgb * 2.0;
    s += srcTex.SampleLevel(linearClamp, uv + float2(0.0,  d.y), 0).rgb * 2.0;
    s += srcTex.SampleLevel(linearClamp, uv + float2(-d.x, -d.y), 0).rgb;
    s += srcTex.SampleLevel(linearClamp, uv + float2( d.x, -d.y), 0).rgb;
    s += srcTex.SampleLevel(linearClamp, uv + float2(-d.x,  d.y), 0).rgb;
    s += srcTex.SampleLevel(linearClamp, uv + float2( d.x,  d.y), 0).rgb;

    return float4(s * (1.0 / 16.0) * pc.intensity, 1.0);
}
