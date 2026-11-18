// Screen-space crease/outline: detects object-ID, normal and linearized
// reverse-Z depth discontinuities from a G-buffer and draws dark lines.
[[vk::binding(0, 0)]] Texture2D<float4> gColor;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
struct Push
{
    float4 params; // near, far, id threshold, depth threshold
    float4 screen; // width, height, unused, unused
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

float linearDepth(float z)
{
    // Reverse-Z with finite far: z=1 -> near, z=0 -> far.
    return pc.params.x * pc.params.y /
           max(pc.params.x + z * (pc.params.y - pc.params.x), 1e-6);
}

bool isEdge(int2 p, int2 neighbor)
{
    int2 dims = int2(pc.screen.xy);
    if (neighbor.x < 0 || neighbor.y < 0 || neighbor.x >= dims.x || neighbor.y >= dims.y) return true;

    float4 a = gColor.Load(int3(p, 0));
    float4 b = gColor.Load(int3(neighbor, 0));
    if (abs(a.r - b.r) > pc.params.z) return true;
    if (dot(normalize(a.gba), normalize(b.gba)) < 0.92) return true;

    float da = linearDepth(gDepth.Load(int3(p, 0)));
    float db = linearDepth(gDepth.Load(int3(neighbor, 0)));
    if (abs(da - db) > pc.params.w * max(1.0, da)) return true;
    return false;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    int2 p = int2(input.uv * pc.screen.xy);
    bool edge = isEdge(p, p + int2(1, 0)) || isEdge(p, p + int2(0, 1));
    return edge ? float4(0.02, 0.02, 0.04, 1.0) : float4(1.0, 1.0, 1.0, 1.0);
}
