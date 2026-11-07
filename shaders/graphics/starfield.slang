// Procedural starfield: position-less fullscreen triangle, push constants only.
// Vertex shader deliberately declares no push constants (it needs none);
// the fragment shader reads them. Must match PushConstants in
// src/environment/starfield_renderer.cpp (std430: time @0, intensity @4,
// resolution @8; 16 bytes total).

struct PushConstants
{
    float time;
    float intensity;
    float2 resolution;
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    // Fullscreen triangle: vertices at (-1,-1), (3,-1), (-1,3).
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };

    VSOutput out;
    float2 pos = positions[vertexIndex];
    out.position = float4(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

// Deterministic per-cell hash -> [0, 1). Integer bit-mixing avoids the
// float precision loss of sin/dot hashes at large coordinates.
float hash21(float2 p)
{
    uint2 u = uint2(asuint(p.x), asuint(p.y));
    u = u * 1664525u + 1013904223u;
    u.x += u.y * 1664525u;
    u.y += u.x * 1664525u;
    u = u ^ (u >> 16u);
    u.x += u.y * 1664525u;
    return float(u.x) * (1.0 / 4294967296.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Star cells sized in pixels so density is resolution independent.
    const float cellSize = 7.0;
    float2 grid = (input.uv * pc.resolution) / max(cellSize, 1.0);

    float2 cell = floor(grid);
    float2 f = frac(grid);

    // Only ~18% of cells spawn a star (sparsity threshold), at a hashed
    // position clamped to the cell center so halos never bleed across
    // cell boundaries and get clipped into squares.
    float h = hash21(cell);
    float2 starPos = clamp(
        float2(hash21(cell + 19.19), hash21(cell + 7.7)),
        float2(0.25, 0.25),
        float2(0.75, 0.75));
    float d = length(f - starPos);

    // Sharp core with a soft halo. Star positions are clamped to
    // [0.25, 0.75], so the nearest cell wall is 0.25 away; both radii
    // stay <= 0.24 so intensity reaches 0 before any border.
    float core = smoothstep(0.08, 0.0, d);
    float halo = smoothstep(0.24, 0.0, d) * 0.25;

    // Twinkle.
    float twinkle = 0.75 + 0.25 * sin(pc.time * (1.0 + h * 3.0) + h * 40.0);

    // Tint: mix between cool blue-white and warm yellow-white by hash.
    float3 tint = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.9, 0.75), hash21(cell + 3.3));

    float starThreshold = step(0.82, h);
    float3 star = tint * (core + halo) * twinkle * pc.intensity * starThreshold;

    // Deep-space background.
    float3 background = float3(0.008, 0.01, 0.025);

    return float4(background + star, 1.0);
}
