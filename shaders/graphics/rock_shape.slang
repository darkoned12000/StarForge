// Shared helpers for the instanced rock field.
//
// The shaded mesh (rock_field.slang) and its inverted-hull outline
// (rock_outline.slang) must agree exactly on the per-instance shape,
// otherwise the ink outline traces a different silhouette than the rock it
// belongs to. The displacement and the dissolve dither therefore live here
// and are included by both shaders.

// Rotate vector v by unit quaternion q.
float3 rockRotate(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// Stable per-instance hash in [0, 1).
float rockHash1(float x)
{
    return fract(sin(x * 127.1) * 43758.5453);
}

// 3D value noise with a smoothstep interpolant, used for surface detail.
float rockHash3(float3 p)
{
    return fract(sin(dot(p, float3(127.1f, 311.7f, 74.7f))) * 43758.5453f);
}

float rockNoise3(float3 p)
{
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(rockHash3(i), rockHash3(i + float3(1, 0, 0)), f.x),
                     lerp(rockHash3(i + float3(0, 1, 0)), rockHash3(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(rockHash3(i + float3(0, 0, 1)), rockHash3(i + float3(1, 0, 1)), f.x),
                     lerp(rockHash3(i + float3(0, 1, 1)), rockHash3(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

// Smooth, coherent per-instance shape offset.
//
// The previous implementation jittered every vertex with an independent
// hash, which read as a spiky, crumpled silhouette. Sampling a continuous
// noise field instead means neighbouring vertices move together, so the
// rock keeps a believable rounded-but-lumpy outline while still differing
// from rock to rock.
float rockDisplace(float3 localPos, uint seed)
{
    float3 p = localPos * 2.1f + float(seed % 977u) * 0.371f;
    float h = rockNoise3(p) * 0.65f + rockNoise3(p * 2.6f) * 0.35f;
    return 0.86f + 0.30f * h;
}

// 4x4 ordered dither, values 0..15 normalised to 0..1.
//
// Used to dissolve rocks as they cross the edge of the streamed field. An
// ordered pattern reads as a smooth fade instead of the flickering noise a
// random dither produces, and it needs no blending, so the opaque depth
// pipeline stays intact.
float rockBayer4(float2 p)
{
    int2 c = int2(floor(p)) & 3;
    int2 half = c >> 1;
    int2 low = c & 1;

    // The 2x2 Bayer offsets are {0, 2, 3, 1} indexed by (x + 2y).
    int i2 = low.x + 2 * low.y;
    int o2 = (i2 == 0) ? 0 : ((i2 == 1) ? 2 : ((i2 == 2) ? 3 : 1));

    // The 4x4 matrix is built from the 2x2 one: the top-left block is
    // 4*M, the top-right 4*M+2, the bottom-left 4*M+3, bottom-right 4*M+1.
    int i4 = half.x + 2 * half.y;
    int o4 = (i4 == 0) ? 0 : ((i4 == 1) ? 2 : ((i4 == 2) ? 3 : 1));

    return float(4 * o2 + o4) / 16.0;
}
