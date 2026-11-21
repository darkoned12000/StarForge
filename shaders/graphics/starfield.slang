// Procedural starfield: position-less fullscreen triangle, camera-basis
// push constants only. Stars are pinned to world-space directions (no
// screen-locked stickers): the fragment shader reconstructs the view
// direction directly from the camera basis so camera translation cannot
// shift the sky. Alpha stays opaque (1) because this is the base sky pass.
// Output is linear HDR: bright stars exceed 1.0 so a bloom pass can catch
// them. Push: camera basis + {time, intensity, aspect, tanHalfFov} = 64 B.

#include "output_shoulder.slang"

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

// PCG hash: avoids the lattice correlation of the old LCG chain.
uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float hash31(int3 p)
{
    uint u = pcgHash(uint(p.x));
    u = pcgHash(u ^ uint(p.y) + 0x9e3779b9u);
    u = pcgHash(u ^ uint(p.z) + 0x85ebca6bu);
    return float(u) * (1.0 / 4294967296.0);
}

// One layer of direction-cell stars. Radius is clamped to the pixel footprint
// with energy conserved, so sub-pixel stars do not shimmer or disappear.
float3 starLayer(float3 dir, float cells, float density, float radius,
                 float gain, int seed, float px)
{
    float3 s = dir * cells;
    int3 baseCell = int3(floor(s));
    int3 cell = baseCell + int3(seed, seed * 7, seed * 13); // hash namespace only
    if (hash31(cell) > density) return float3(0.0, 0.0, 0.0);

    float3 jitter = float3(hash31(cell + int3(19, 19, 19)),
                           hash31(cell + int3(7, 7, 7)),
                           hash31(cell + int3(13, 13, 13)));
    // Direction must come from the UNSHIFTED cell, otherwise every star is
    // displaced by the seed offset and lands outside the camera view.
    float3 starDir = normalize(float3(baseCell) + jitter);

    float mag = pow(1.0 - hash31(cell + int3(31, 31, 31)), 6.0);
    float r = radius * (0.7 + sqrt(mag));
    float rEff = max(r, px * 0.75);
    // Softer energy conservation than r²/rEff²: that made widened stars
    // almost invisible at small test resolutions.
    float energy = rEff > r ? sqrt(r / rEff) : 1.0f;
    energy = max(energy, 0.45f);

    float d = length(dir - starDir);
    float sigma = max(rEff * 0.5, 1e-5f);
    float f = exp(-d * d / (2.0f * sigma * sigma)) * energy;

    // Fade near cell boundaries so a star is never cut into a wedge.
    float3 fc = frac(s);
    float boundary = min(min(min(fc.x, 1.0f - fc.x), min(fc.y, 1.0f - fc.y)),
                         min(fc.z, 1.0f - fc.z));
    f *= smoothstep(0.0f, 3.0f * rEff * cells, boundary);

    // Blackbody-ish temperature: orange -> warm white -> cool blue.
    float t = hash31(cell + int3(3, 3, 3));
    float3 tint = t < 0.5f
        ? lerp(float3(1.0f, 0.58f, 0.36f), float3(1.0f, 0.96f, 0.90f), t * 2.0f)
        : lerp(float3(1.0f, 0.96f, 0.90f), float3(0.58f, 0.74f, 1.0f), (t - 0.5f) * 2.0f);

    float twinkle = 1.0f;
    float twSeed = hash31(cell + int3(47, 47, 47));
    if (twSeed > 0.75f)
        twinkle = 0.85f + 0.15f * sin(pc.timeIntensityAspectTan.x * (0.12f + twSeed * 0.3f) + twSeed * 40.0f);

    // HDR luminosity: a few stars well above 1.0 for bloom to catch.
    return tint * f * gain * (0.4f + mag * 10.0f) * twinkle;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(pc.right.xyz * input.ndc.x * pc.timeIntensityAspectTan.z * pc.timeIntensityAspectTan.w +
                           pc.up.xyz * input.ndc.y * pc.timeIntensityAspectTan.w +
                           pc.forward.xyz);

    // Derive pixel footprint from the fullscreen NDC (exact 2/resolution),
    // not from fwidth(dir): the latter varies with camera translation and
    // breaks world-pinned stability at star-cell boundaries.
    float px = pc.timeIntensityAspectTan.w * fwidth(input.ndc.y) * 2.0f;
    float gain = pc.timeIntensityAspectTan.y;

    float3 stars = float3(0.0f, 0.0f, 0.0f);
    stars += starLayer(dir, 12.0f, 0.30f, 0.0022f, 2.0f, 1, px);
    stars += starLayer(dir, 40.0f, 0.24f, 0.0014f, 1.3f, 2, px);
    stars += starLayer(dir, 110.0f, 0.20f, 0.0009f, 0.9f, 3, px);
    stars += starLayer(dir, 280.0f, 0.16f, 0.0006f, 0.6f, 4, px);

    // A broad galactic great circle gives the sky large-scale structure.
    float band = exp(-pow(dot(dir, normalize(float3(0.35f, 0.86f, -0.37f))) * 3.2f, 2.0f));
    stars += starLayer(dir, 70.0f, 0.12f + 0.25f * band, 0.0011f,
                       0.7f + 1.2f * band, 5, px);

    // Subtle deep-space gradient: faint blue above, warm dust below.
    float latitude = dir.y * 0.5f + 0.5f;
    float3 background = lerp(float3(0.010f, 0.008f, 0.014f),
                             float3(0.006f, 0.010f, 0.022f), latitude);
    background *= 1.0f + 0.4f * band;

    return float4(outputColor(background + stars), 1.0f);
}
