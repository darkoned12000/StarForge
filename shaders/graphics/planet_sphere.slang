// M15.0.4 planet ray-sphere billboard. A fullscreen triangle ray-casts the
// planet sphere per pixel, writes projective depth so nearer objects occlude
// it, shades toon diffuse bands and a Fresnel limb. All coordinates are in
// the camera-relative space of the caller's viewProj (Vulkan [0,1] depth).

#include "output_shoulder.slang"

struct Push
{
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 cameraPos;    // xyz = camera position (camera-relative), w unused
    float4 planet;       // xyz = planet centre, w = radius
    float4 sunDir;       // xyz = direction to sun, w unused
    float4 baseColor;
    float4 shadowColor;
    float4 rimColor;
    float4 params;       // x = fresnel power, y = rim strength, z/w unused
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

float3 rayDirection(float2 uv)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 nearP = mul(pc.invViewProj, float4(ndc, 0.0, 1.0));
    float4 farP = mul(pc.invViewProj, float4(ndc, 1.0, 1.0));
    return normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
}

// Deterministic hash / value noise used to sculpt continents and ridges.
float hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float valueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + float3(0, 0, 0));
    float n100 = hash13(i + float3(1, 0, 0));
    float n010 = hash13(i + float3(0, 1, 0));
    float n110 = hash13(i + float3(1, 1, 0));
    float n001 = hash13(i + float3(0, 0, 1));
    float n101 = hash13(i + float3(1, 0, 1));
    float n011 = hash13(i + float3(0, 1, 1));
    float n111 = hash13(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
                lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

float fbm(float3 p)
{
    float v = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i)
    {
        v += amplitude * valueNoise(p);
        p = p * 2.03 + float3(11.3, 7.1, 3.7);
        amplitude *= 0.5;
    }
    return v;
}

// Continents (large fbm), ridged mountain chains and fine rock detail in the
// planet's rotating frame. The range stays near [-1, 1] for the ink thresholds.
float surfacePattern(float3 p)
{
    float continents = (fbm(p * 1.7) - 0.5) * 2.1;
    float ridge = 1.0 - abs(fbm(p * 4.3 + float3(5.2, 1.3, 9.7)) * 2.0 - 1.0);
    float detail = (fbm(p * 10.0 + float3(3.1, 8.7, 2.2)) - 0.5) * 0.28;
    return continents + (ridge - 0.5) * 0.45 + detail;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, out float depth : SV_Depth) : SV_Target
{
    float3 ro = pc.cameraPos.xyz;
    float3 rd = rayDirection(input.uv);
    float3 centre = pc.planet.xyz;
    float radius = pc.planet.w;

    float3 oc = ro - centre;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) discard;

    float root = sqrt(disc);
    float t = -b - root;
    if (t <= 0.0) t = -b + root;
    if (t <= 0.0) discard;

    float3 hit = ro + rd * t;
    float3 N = normalize(hit - centre);
    float3 V = normalize(ro - hit);
    float3 L = normalize(pc.sunDir.xyz);

    float spin = pc.params.z;
    float cs = cos(spin);
    float sn = sin(spin);
    float3 local = float3(cs * N.x - sn * N.z, N.y, sn * N.x + cs * N.z);
    float pattern = surfacePattern(local);
    float latitude = abs(local.y);

    // Biome palette: oceans shift deep->shallow, land shifts beach->grass->
    // desert->rock by height and latitude, with polar ice caps.
    float3 deepSea   = float3(0.02f, 0.12f, 0.40f);
    float3 shallow   = float3(0.06f, 0.36f, 0.60f);
    float3 beach     = float3(0.72f, 0.66f, 0.46f);
    float3 grass     = float3(0.14f, 0.38f, 0.20f);
    float3 desert    = float3(0.62f, 0.46f, 0.24f);
    float3 rock      = float3(0.38f, 0.34f, 0.30f);
    float3 ice       = float3(0.86f, 0.92f, 0.98f);

    float shore = smoothstep(-0.45f, -0.02f, pattern);
    float3 ocean = lerp(deepSea, shallow, shore);

    float height = smoothstep(0.0f, 0.55f, pattern);
    float3 landColor = lerp(beach, grass, smoothstep(0.02f, 0.18f, pattern));
    landColor = lerp(landColor, desert, smoothstep(0.18f, 0.42f, pattern) *
                     (1.0f - smoothstep(0.45f, 0.75f, latitude)));
    landColor = lerp(landColor, rock, height);

    float land = smoothstep(-0.07f, 0.01f, pattern);
    float3 surface = lerp(ocean, landColor, land);
    // Clouds: a second fbm at a different frequency, drifting faster.
    float clouds = smoothstep(0.55f, 0.78f, fbm(local * 3.1f + float3(spin * 0.7f, 2.0f, 1.0f)));
    surface = lerp(surface, float3(0.92f, 0.95f, 0.99f), clouds * 0.65f);
    // Polar caps (cloud-independent, on top).
    float iceMask = smoothstep(0.80f, 0.93f, latitude);
    surface = lerp(surface, ice, iceMask);

    // Continuous wrap lighting: no hard toon bands. A smooth terminator plus
    // a gentle gamma curve reads as a lit sphere instead of stepped rings.
    float ndl_raw = dot(N, L);
    float wrap = saturate((ndl_raw + 0.18f) / 1.18f);
    float shade = 0.12f + 0.88f * pow(wrap, 1.25f);
    float3 color = surface * shade * lerp(pc.baseColor.rgb, float3(1.0f), 0.55f);

    // Warm sunset band blends smoothly across the terminator.
    float sunset = exp(-pow((ndl_raw - 0.02f) * 4.5f, 2.0f));
    color += float3(1.0f, 0.42f, 0.16f) * sunset * 0.35f;

    // Atmospheric limb: broad, gradual falloff on the sunlit side only, so
    // surface light and atmosphere share the same soft transition.
    float limb = pow(1.0f - saturate(dot(N, V)), pc.params.x);
    float sunSide = smoothstep(-0.30f, 0.55f, ndl_raw);
    float3 atmo = lerp(pc.rimColor.rgb, float3(1.0f, 0.55f, 0.30f), sunset * 0.45f);
    color += atmo * limb * sunSide * pc.params.y;

    // Night-side city lights: sparse warm points on land, HDR (>1) so bloom
    // can catch them. Kept painterly - no hard ink lines on planets.
    float night = 1.0f - smoothstep(-0.10f, 0.08f, ndl_raw);
    float cities = smoothstep(0.62f, 0.80f, fbm(local * 15.0f + float3(4.0f, 9.0f, 2.0f)));
    cities *= land * (1.0f - iceMask) * (1.0f - clouds);
    color += float3(1.0f, 0.72f, 0.36f) * cities * night * 1.6f;

    float4 clip = mul(pc.viewProj, float4(hit, 1.0f));
    depth = saturate(clip.z / clip.w);
    return float4(outputColor(color), 1.0f);
}
