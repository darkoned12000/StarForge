// Toon (cel-shaded) shader with bilinear BDA texture sampling, ink-masked
// comic specular/rim highlights, and an inverted-hull outline pass (mode = 1).
// Geometry is a static cube (36 vertices) until a caller supplies mesh data
// via pc.vertexAddress. DrawData / PushConstants layouts are unchanged.

static const float3 kCubePositions[36] = {
    float3(-0.5,-0.5, 0.5), float3(0.5,-0.5,0.5), float3(0.5,0.5,0.5),
    float3(0.5,0.5,0.5), float3(-0.5,0.5,0.5), float3(-0.5,-0.5,0.5),
    float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5), float3(-0.5,0.5,-0.5),
    float3(-0.5,0.5,-0.5), float3(0.5,0.5,-0.5), float3(0.5,-0.5,-0.5),
    float3(-0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5), float3(-0.5,0.5,0.5),
    float3(-0.5,0.5,0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,-0.5),
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(0.5,0.5,0.5), float3(0.5,-0.5,0.5),
    float3(-0.5,0.5,0.5), float3(0.5,0.5,0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,0.5),
    float3(-0.5,-0.5,-0.5), float3(0.5,-0.5,-0.5), float3(0.5,-0.5,0.5),
    float3(0.5,-0.5,0.5), float3(-0.5,-0.5,0.5), float3(-0.5,-0.5,-0.5),
};

static const float3 kCubeNormals[36] = {
    float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),
    float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),
    float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),
    float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),
    float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),
    float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),
};

static const float3 kCubeOutlineNormals[36] = {
    float3(-0.577,-0.577, 0.577), float3(0.577,-0.577,0.577), float3(0.577,0.577,0.577),
    float3(0.577,0.577,0.577), float3(-0.577,0.577,0.577), float3(-0.577,-0.577,0.577),
    float3(0.577,-0.577,-0.577), float3(-0.577,-0.577,-0.577), float3(-0.577,0.577,-0.577),
    float3(-0.577,0.577,-0.577), float3(0.577,0.577,-0.577), float3(0.577,-0.577,-0.577),
    float3(-0.577,-0.577,-0.577), float3(-0.577,-0.577,0.577), float3(-0.577,0.577,0.577),
    float3(-0.577,0.577,0.577), float3(-0.577,0.577,-0.577), float3(-0.577,-0.577,-0.577),
    float3(0.577,-0.577,0.577), float3(0.577,-0.577,-0.577), float3(0.577,0.577,-0.577),
    float3(0.577,0.577,-0.577), float3(0.577,0.577,0.577), float3(0.577,-0.577,0.577),
    float3(-0.577,0.577,0.577), float3(0.577,0.577,0.577), float3(0.577,0.577,-0.577),
    float3(0.577,0.577,-0.577), float3(-0.577,0.577,-0.577), float3(-0.577,-0.577,0.577),
    float3(-0.577,-0.577,-0.577), float3(0.577,-0.577,-0.577), float3(0.577,-0.577,0.577),
    float3(0.577,-0.577,0.577), float3(-0.577,-0.577,0.577), float3(-0.577,-0.577,-0.577),
};

struct DrawData
{
    float4x4 modelView;
    float4 lightDirView;
    float4 baseColor;
    float4 shadowColor;
    float4 midColor;
    float4 params; // x: bandCount, y: rimStrength, z: outline width, w: capability bits
    float4 emissive;
    float4 fresnel;
    float4 material;
    float4 highlight;
    uint64_t textureAddress;
    uint     textureWidth;
    uint     textureHeight;
    uint     pad0;
    uint     pad1;
    uint     pad2;
    uint     pad3;
};

struct PushConstants
{
    float4x4 mvp;
    uint64_t drawDataAddress;
    uint64_t vertexAddress;
    uint mode;
    uint vertexCount;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] PushConstants pc;

typedef vk::BufferPointer<DrawData> DrawDataPtr;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normalView : NORMAL0;
    float3 positionView : POSITION0;
    float2 uv : TEXCOORD0;
};

float aastep(float threshold, float value)
{
    float afwidth = fwidth(value) * 0.7071;
    return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

float4 sampleTexelWrapped(uint64_t addr, uint w, uint h, int x, int y)
{
    int iw = int(w);
    int ih = int(h);
    uint wx = uint(((x % iw) + iw) % iw);
    uint wy = uint(((y % ih) + ih) % ih);
    typedef vk::BufferPointer<uint> PixelPtr;
    PixelPtr pixel = { addr + uint64_t(wy * w + wx) * 4u };
    uint packed = pixel.Get();
    return float4(
        float(packed & 255u),
        float((packed >> 8u) & 255u),
        float((packed >> 16u) & 255u),
        float((packed >> 24u) & 255u)
    ) / 255.0f;
}

float4 sampleTextureBilinear(uint64_t addr, uint w, uint h, float2 uv)
{
    float2 texelPos = uv * float2(float(w), float(h)) - 0.5f;
    int2 base = int2(floor(texelPos));
    float2 f = frac(texelPos);

    float4 c00 = sampleTexelWrapped(addr, w, h, base.x,     base.y);
    float4 c10 = sampleTexelWrapped(addr, w, h, base.x + 1, base.y);
    float4 c01 = sampleTexelWrapped(addr, w, h, base.x,     base.y + 1);
    float4 c11 = sampleTexelWrapped(addr, w, h, base.x + 1, base.y + 1);

    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VSOutput output;
    DrawDataPtr drawPtr = { pc.drawDataAddress };
    DrawData draw = drawPtr.Get();
    const uint caps = uint(draw.params.w + 0.5);
    uint idx = min(vertexIndex, 35u);
    float3 objectPos = kCubePositions[idx];
    float3 objectNormal = kCubeNormals[idx];
    float3 outlineNormal = kCubeOutlineNormals[idx];
    float2 texcoord = float2(0.0, 0.0);

    if (pc.vertexAddress != 0u && vertexIndex < pc.vertexCount)
    {
        typedef vk::BufferPointer<float4> Float4Ptr;
        uint64_t base = pc.vertexAddress;
        if ((caps & 32u) != 0u)
        {
            base += uint64_t(vertexIndex) * 80u;
            Float4Ptr pp = { base };
            Float4Ptr np = { base + 16u };
            Float4Ptr op = { base + 32u };
            Float4Ptr up = { base + 48u };
            objectPos = pp.Get().xyz;
            objectNormal = np.Get().xyz;
            outlineNormal = op.Get().xyz;
            texcoord = up.Get().xy;
        }
        else
        {
            base += uint64_t(vertexIndex) * 48u;
            Float4Ptr pp = { base };
            Float4Ptr np = { base + 16u };
            Float4Ptr op = { base + 32u };
            objectPos = pp.Get().xyz;
            objectNormal = np.Get().xyz;
            outlineNormal = op.Get().xyz;
        }
    }

    if (pc.mode > 0)
    {
        objectPos += outlineNormal * draw.params.z;
    }

    float4 viewPos = mul(draw.modelView, float4(objectPos, 1.0));
    float3 viewNormal = normalize(mul(draw.modelView, float4(objectNormal, 0.0)).xyz);

    output.position = mul(pc.mvp, float4(objectPos, 1.0));
    output.normalView = viewNormal;
    output.positionView = viewPos.xyz;
    output.uv = texcoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    DrawDataPtr drawPtr = { pc.drawDataAddress };
    DrawData draw = drawPtr.Get();

    if (pc.mode > 0)
    {
        return float4(0.03, 0.03, 0.04, 1.0);
    }

    float3 normal = normalize(input.normalView);
    float3 lightDir = normalize(draw.lightDirView.xyz);
    float3 viewDir = normalize(-input.positionView);
    uint caps = uint(draw.params.w + 0.5);

    // Sample baseColorTexture with bilinear filtering when present.
    float4 sampled = float4(1.0, 1.0, 1.0, 1.0);
    bool hasTex = ((caps & 32u) != 0u && draw.textureAddress != 0u &&
                   draw.textureWidth > 0u && draw.textureHeight > 0u);
    if (hasTex)
    {
        sampled = sampleTextureBilinear(draw.textureAddress, draw.textureWidth,
                                        draw.textureHeight, input.uv);
    }

    // Dark painted ink lines mask specular/rim so panel seams stay crisp.
    float texLuma = dot(sampled.rgb, float3(0.299, 0.587, 0.114));
    float inkMask = hasTex ? smoothstep(0.10, 0.28, texLuma) : 1.0;

    float ndotl = max(dot(normal, lightDir), 0.0);

    // Anti-aliased cel bands.
    float bandCount = max(draw.params.x, 2.0);
    float rawBand = ndotl * bandCount;
    float bandWidth = fwidth(rawBand) * 0.7071;
    float nearestBand = round(rawBand);
    float steppedBand = nearestBand - 1.0 + smoothstep(-bandWidth, bandWidth, rawBand - nearestBand);
    float banded = clamp(steppedBand / max(bandCount - 1.0, 1.0), 0.0, 1.0);

    // Three-stop diffuse ramp (shadow -> mid -> lit).
    float3 mid = draw.midColor.rgb;
    if (draw.midColor.w == 0.0)
    {
        mid = lerp(draw.shadowColor.rgb, draw.baseColor.rgb, 0.5);
    }
    float3 diffuse = banded < 0.5
        ? lerp(draw.shadowColor.rgb, mid, banded * 2.0)
        : lerp(mid, draw.baseColor.rgb, (banded - 0.5) * 2.0);
    diffuse *= sampled.rgb;

    // Crisp comic specular, masked out inside painted ink lines.
    float3 halfVec = normalize(lightDir + viewDir);
    float ndoth = max(dot(normal, halfVec), 0.0);
    float spec = aastep(0.65, pow(ndoth, 32.0)) * 0.7 * aastep(0.05, ndotl) * inkMask;

    // Cool rim highlight along silhouettes.
    float facing = 1.0 - max(dot(normal, viewDir), 0.0);
    float rim = aastep(0.6, pow(facing, 3.0)) * draw.params.y * inkMask;

    float3 color = diffuse + (spec + rim) * draw.baseColor.rgb;

    // Fourth highlight ramp stop.
    if (draw.highlight.w > 0.0)
    {
        color = lerp(color, draw.highlight.rgb * sampled.rgb,
                     smoothstep(draw.highlight.w - 0.15, draw.highlight.w + 0.15, banded));
    }

    // Emissive glow (modulated by the texture so cores have hot centres).
    if ((caps & 1u) != 0u)
    {
        float3 emTex = hasTex ? sampled.rgb : float3(1.0, 1.0, 1.0);
        color += draw.emissive.rgb * draw.emissive.w * emTex;
    }

    // Fresnel shield rim / atmospheric scattering gradient.
    if ((caps & 2u) != 0u || (caps & 8u) != 0u)
    {
        float fresnel = pow(facing, 3.0);
        if ((caps & 2u) != 0u)
        {
            color = lerp(color, draw.fresnel.rgb, clamp(fresnel * draw.fresnel.w, 0.0, 1.0));
        }
        else
        {
            color += lerp(draw.shadowColor.rgb, draw.fresnel.rgb, fresnel) * draw.fresnel.w;
        }
    }

    // Faceted asteroid lighting from macro-normal quantization.
    if ((caps & 4u) != 0u)
    {
        float3 facet = normalize(floor(normal * 4.0 + 0.5) / 4.0);
        float facetLight = max(dot(facet, lightDir), 0.0);
        color = lerp(color, color * (0.55 + 0.45 * facetLight), draw.material.x);
    }

    // Shadows, light attenuation and surface detail modulation.
    if ((caps & 16u) != 0u)
    {
        float attenuation = 1.0 / (1.0 + draw.material.z * dot(input.positionView, input.positionView));
        color *= lerp(1.0, attenuation, step(0.001, draw.material.z));
        color = lerp(color, draw.shadowColor.rgb * sampled.rgb,
                     draw.material.y * (1.0 - aastep(0.25, ndotl)));
        color *= lerp(1.0, 0.85 + 0.15 * sin(input.positionView.x * draw.material.w * 10.0),
                      step(0.001, draw.material.w));
    }

    return float4(color, draw.baseColor.a * sampled.a);
}
