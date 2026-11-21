// Bloom downsample: 13-tap filter (Jimenez, "Next Generation Post Processing
// in Call of Duty: Advanced Warfare"). The first pass (firstPass = 1) also
// applies a soft-knee brightness threshold and Karis averaging, which stops a
// single sub-pixel star from flickering into a large bloom blob.
//
// Chain: HDR scene (full res) -> mip0 (1/2) -> mip1 (1/4) -> ... (4-6 levels).
// Pipeline: no blending, no depth. Each pass renders a fullscreen triangle
// into the next smaller target, sampling the previous one (descriptor set 0:
// binding 0 = source image, binding 1 = linear clamp sampler).
// Push: 32 bytes. texelSize is 1 / (source width, source height).

struct PushConstants
{
    float2 texelSize;
    float threshold;  // brightness (max channel) where bloom starts, e.g. 1.0
    float knee;       // soft transition width, e.g. 0.5
    uint firstPass;
    float pad0;
    float pad1;
    float pad2;
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

float3 prefilter(float3 c)
{
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    soft = soft * soft / (4.0 * pc.knee + 1e-4);
    float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-4);
    return c * contribution;
}

float3 fetch(float2 uv, float2 offsetTexels)
{
    float3 c = srcTex.SampleLevel(linearClamp, uv + offsetTexels * pc.texelSize, 0).rgb;
    return (pc.firstPass != 0u) ? prefilter(c) : c;
}

float karis(float3 c)
{
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;

    float3 a = fetch(uv, float2(-2, -2));
    float3 b = fetch(uv, float2( 0, -2));
    float3 c = fetch(uv, float2( 2, -2));
    float3 d = fetch(uv, float2(-2,  0));
    float3 e = fetch(uv, float2( 0,  0));
    float3 f = fetch(uv, float2( 2,  0));
    float3 g = fetch(uv, float2(-2,  2));
    float3 h = fetch(uv, float2( 0,  2));
    float3 i = fetch(uv, float2( 2,  2));
    float3 j = fetch(uv, float2(-1, -1));
    float3 k = fetch(uv, float2( 1, -1));
    float3 l = fetch(uv, float2(-1,  1));
    float3 m = fetch(uv, float2( 1,  1));

    float3 result;
    if (pc.firstPass != 0u)
    {
        // Karis-weighted groups of four taps.
        float3 g0 = (a + b + d + e) * 0.25;
        float3 g1 = (b + c + e + f) * 0.25;
        float3 g2 = (d + e + g + h) * 0.25;
        float3 g3 = (e + f + h + i) * 0.25;
        float3 g4 = (j + k + l + m) * 0.25;
        float w0 = 0.125 * karis(g0);
        float w1 = 0.125 * karis(g1);
        float w2 = 0.125 * karis(g2);
        float w3 = 0.125 * karis(g3);
        float w4 = 0.500 * karis(g4);
        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    }
    else
    {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }
    return float4(result, 1.0);
}
