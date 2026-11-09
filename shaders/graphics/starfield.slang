// Procedural starfield: position-less fullscreen triangle, camera-basis
// push constants only. Stars are pinned to world-space directions (no
// screen-locked stickers): the fragment shader reconstructs the view
// direction directly from the camera basis so camera translation cannot
// shift the sky. Alpha stays opaque (1) because this is the base sky pass;
// the pipeline created for it has blending disabled.
// Push: camera basis (right/up/forward) + {time, intensity, aspect,
// tanHalfFov} = 64 bytes.

struct PushConstants
{
    float4 right;
    float4 up;
    float4 forward;
    float4 timeIntensityAspectTan;
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOutput output;
    float2 pos = positions[vertexId];
    output.position = float4(pos, 0.0, 1.0);
    output.ndc = pos;
    return output;
}

// 32-bit integer hash for uniform distribution without float precision artifacts
uint hash32(uint x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

float hash31(int3 p)
{
    uint u = hash32(uint(p.x));
    u = hash32(u ^ uint(p.y));
    u = hash32(u ^ uint(p.z));
    return float(u) * (1.0 / 4294967296.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // World-space view direction through this pixel.
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.timeIntensityAspectTan.z * pc.timeIntensityAspectTan.w +
                           pc.up.xyz * input.ndc.y * pc.timeIntensityAspectTan.w +
                           pc.forward.xyz);

    // 3D direction cells (pinned to the sky sphere; turning the camera
    // moves stars correctly, unlike screen-space grids). Cell size trades
    // star density against angular core size (visible at test resolution).
    const float cellSize = 12.0;
    int3 cell = int3(floor(dir * cellSize));

    // One star (sometimes) per cell, jittered inside its own cell so the
    // pixel evaluating that cell can actually see it.
    float h = hash31(cell);
    float3 jitter = float3(hash31(cell + int3(19, 19, 19)), hash31(cell + int3(7, 7, 7)),
                           hash31(cell + int3(13, 13, 13)));
    float3 starDir = normalize(float3(cell) + 0.2 + 0.6 * jitter);
    // Power-law brightness: most faint, few bright; drives apparent size.
    float mag = pow(1.0 - hash31(cell + int3(31, 31, 31)), 6.0);
    float starScale = 0.6 + mag * 1.6;

    // Chord distance from the view ray to the star direction.
    float d = length(dir - starDir) * cellSize / starScale;
    float dPhys = length(dir - starDir) * cellSize;

    // Sharp core (brightness-scaled) with a halo capped at physical 0.24
    // so it stays tightly tied to the star's cell.
    float core = smoothstep(0.08, 0.0, d);
    float halo = smoothstep(0.24, 0.0, dPhys) * 0.25;

    // Twinkle (brighter stars twinkle faster).
    float twinkle = 0.75 + 0.25 * sin(pc.timeIntensityAspectTan.x * (1.0 + h * 3.0 + mag * 4.0) + h * 40.0);

    // Tint: mix between cool blue-white and warm yellow-white by hash.
    float3 tint = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.9, 0.75), hash31(cell + int3(3, 3, 3)));

    float starThreshold = step(0.82, h);
    float3 star = tint * (core + halo) * (0.6 + mag * 2.4) * twinkle * pc.timeIntensityAspectTan.y * starThreshold;

    // Deep-space background.
    float3 background = float3(0.008, 0.01, 0.025);

    return float4(background + star, 1.0);
}
