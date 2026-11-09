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
    const float3 scaled = dir * cellSize;
    const int3 cell = int3(floor(scaled));

    float h = hash31(cell);
    if (h <= 0.88)
    {
        return float4(float3(0.008, 0.01, 0.025), 1.0);
    }

    // One star per direction cell, fully jittered inside the cell for a
    // scattered layout. The pixel-boundary fade below prevents a
    // boundary-adjacent star from being clipped into a wedge without
    // introducing candidate duplicates from neighboring cells.
    float3 jitter = float3(hash31(cell + int3(19, 19, 19)), hash31(cell + int3(7, 7, 7)),
                           hash31(cell + int3(13, 13, 13)));
    float3 starDir = normalize(float3(cell) + jitter);
    // Power-law brightness: most faint, few bright; drives apparent size.
    float mag = pow(1.0 - hash31(cell + int3(31, 31, 31)), 6.0);
    float sizeFactor = 0.7 + sqrt(mag) * 1.1;

    // Chord distance from the view ray to the star direction. The
    // radii are tuned for the 60-degree vertical FOV: a few pixels
    // for the core and a short halo rather than a large soft disc.
    float distance = length(dir - starDir);
    float core = smoothstep(0.002 * sizeFactor, 0.0, distance);
    float haloSigma = 0.004 * (0.7 + sqrt(mag));
    float halo = 0.35 * exp(-distance * distance / (haloSigma * haloSigma));

    // Distance to the nearest direction-cell boundary, in cell units.
    // Fading the halo there prevents it from spilling into the next cell
    // and being cut into a wedge.
    float3 fracCoord = frac(scaled);
    float boundaryDist = min(min(fracCoord.x, 1.0 - fracCoord.x),
                             min(fracCoord.y, 1.0 - fracCoord.y));
    boundaryDist = min(boundaryDist, min(fracCoord.z, 1.0 - fracCoord.z));
    float haloFade = smoothstep(0.0, 0.10, boundaryDist);
    float coreFade = smoothstep(0.0, 0.035, boundaryDist);
    halo *= haloFade;
    core *= coreFade;

    // Twinkle (brighter stars twinkle faster).
    float twinkle = 0.75 + 0.25 * sin(pc.timeIntensityAspectTan.x * (1.0 + h * 3.0 + mag * 4.0) + h * 40.0);
    float brightness = (core * 2.2 + halo * (0.5 + mag)) * twinkle * pc.timeIntensityAspectTan.y;
    float3 tint = lerp(float3(0.75, 0.85, 1.0), float3(1.0, 0.9, 0.75), hash31(cell + int3(3, 3, 3)));

    // Tone-map the star so the tint survives instead of clamping to a
    // flat white UNORM value, while keeping the core near-white.
    float starTone = 1.0 - exp(-brightness * 0.9);
    float3 star = tint * starTone;
    star += (float3(1.0) - star) * clamp(brightness - 1.2, 0.0, 1.0) * core * 0.35;

    // Deep-space background.
    float3 background = float3(0.008, 0.01, 0.025);

    return float4(background + star, 1.0);
}
