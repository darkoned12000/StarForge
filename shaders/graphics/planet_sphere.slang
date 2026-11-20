// M15.0.4 planet ray-sphere billboard. A fullscreen triangle ray-casts the
// planet sphere per pixel, writes projective depth so nearer objects occlude
// it, shades toon diffuse bands and a Fresnel limb. All coordinates are in
// the camera-relative space of the caller's viewProj (Vulkan [0,1] depth).

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

// Cheap deterministic surface pattern in the planet's rotating frame.
float surfacePattern(float3 p)
{
    float v = 0.5 * sin(p.x * 5.0 + sin(p.z * 3.0) * 2.0);
    v += 0.3 * sin(p.y * 7.0 + p.z * 4.0);
    v += 0.2 * sin(p.x * 11.0 - p.y * 9.0);
    return v;
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

    // Rotate the surface coordinate about the planet's axis so continents
    // drift slowly across the terminator; pc.params.z carries the spin angle.
    float spin = pc.params.z;
    float cs = cos(spin);
    float sn = sin(spin);
    float3 local = float3(cs * N.x - sn * N.z, N.y, sn * N.x + cs * N.z);
    float pattern = surfacePattern(local);

    // Comic-ink coastline: crisp anti-aliased continent edge with a dark
    // painted seam, instead of a soft PBR gradient.
    float coastWidth = fwidth(pattern) * 0.7071;
    float land = smoothstep(-0.06 - coastWidth, -0.06 + coastWidth, pattern);
    float coast = 1.0 - smoothstep(0.0, 0.07 + coastWidth, abs(pattern + 0.06));

    // Toon diffuse: three anti-aliased stepped bands.
    float ndl = max(dot(N, L), 0.0);
    float bandCoord = ndl * 3.0;
    float bandWidth = fwidth(bandCoord) * 0.7071;
    float nearestBand = round(bandCoord);
    float stepped = nearestBand - 1.0 + smoothstep(-bandWidth, bandWidth, bandCoord - nearestBand);
    float banded = clamp(stepped / 2.0, 0.0, 1.0);
    float3 color = lerp(pc.shadowColor.rgb, pc.baseColor.rgb, banded);

    // Oceans vs land, with a second tonal step inside the continents.
    color *= lerp(0.55, 1.10, land);
    color *= lerp(0.88, 1.06, smoothstep(0.08, 0.42, pattern));

    // Ink the coastline and the terminator so the night side keeps its shape.
    color = lerp(color, float3(0.025, 0.045, 0.085), coast * 0.75);
    float termWidth = fwidth(ndl) * 0.7071;
    float terminator = 1.0 - smoothstep(0.0, 0.05 + termWidth, abs(ndl - 0.15));
    color *= 1.0 - terminator * 0.35;

    // Fresnel rim: 1 - (n·v)^p, warm rim tint.
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), pc.params.x) * pc.params.y;
    color += pc.rimColor.rgb * fresnel;

    // Projective depth (Vulkan depth range [0,1]).
    float4 clip = mul(pc.viewProj, float4(hit, 1.0));
    depth = saturate(clip.z / clip.w);
    return float4(color, 1.0);
}
