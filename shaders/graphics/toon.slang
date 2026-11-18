// Toon (cel-shaded) cube: quantized diffuse bands, specular, and rim light.
// The same shader drives the inverted-hull outline pass (mode = 1): the
// vertex stage expands positions along view-space normals by a CPU-computed
// view-space width (constant screen-pixel thickness) and the fragment stage
// outputs a flat dark color. Geometry is a static cube (36 vertices,
// per-face normals) until real mesh assets land (M17).
//
// Push constants carry the clip matrix plus a BDA pointer to the
// per-draw block (modelView, light, and the color-ramp data below). This
// keeps the push-constant range at 80 bytes, below Vulkan's guaranteed
// 128-byte minimum.

static const float3 kCubePositions[36] = {
    // Front face (z = +0.5)
    float3(-0.5,-0.5, 0.5), float3(0.5,-0.5,0.5), float3(0.5,0.5,0.5),
    float3(0.5,0.5,0.5), float3(-0.5,0.5,0.5), float3(-0.5,-0.5,0.5),
    // Back face (z = -0.5)
    float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5), float3(-0.5,0.5,-0.5),
    float3(-0.5,0.5,-0.5), float3(0.5,0.5,-0.5), float3(0.5,-0.5,-0.5),
    // Left face (x = -0.5)
    float3(-0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5), float3(-0.5,0.5,0.5),
    float3(-0.5,0.5,0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,-0.5),
    // Right face (x = +0.5)
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(0.5,0.5,0.5), float3(0.5,-0.5,0.5),
    // Top face (y = +0.5)
    float3(-0.5,0.5,0.5), float3(0.5,0.5,0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(-0.5,0.5,-0.5), float3(-0.5,0.5,0.5),
    // Bottom face (y = -0.5)
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

// Smoothed (corner-averaged) normals for the inverted hull. Flat per-face
// normals split hard corners into disconnected plates; averaging the
// adjacent face normals per corner keeps the expanded hull closed. Real
// meshes need baked smooth normals or a screen-space edge pass (M17).
static const float3 kCubeOutlineNormals[36] = {
    // Front face corners: normalize(+-x, +-y, +z)
    float3(-0.577,-0.577, 0.577), float3(0.577,-0.577,0.577), float3(0.577,0.577,0.577),
    float3(0.577,0.577,0.577), float3(-0.577,0.577,0.577), float3(-0.577,-0.577,0.577),
    // Back face corners
    float3(0.577,-0.577,-0.577), float3(-0.577,-0.577,-0.577), float3(-0.577,0.577,-0.577),
    float3(-0.577,0.577,-0.577), float3(0.577,0.577,-0.577), float3(0.577,-0.577,-0.577),
    // Left face corners
    float3(-0.577,-0.577,-0.577), float3(-0.577,-0.577,0.577), float3(-0.577,0.577,0.577),
    float3(-0.577,0.577,0.577), float3(-0.577,0.577,-0.577), float3(-0.577,-0.577,-0.577),
    // Right face corners
    float3(0.577,-0.577,0.577), float3(0.577,-0.577,-0.577), float3(0.577,0.577,-0.577),
    float3(0.577,0.577,-0.577), float3(0.577,0.577,0.577), float3(0.577,-0.577,0.577),
    // Top face corners
    float3(-0.577,0.577,0.577), float3(0.577,0.577,0.577), float3(0.577,0.577,-0.577),
    float3(0.577,0.577,-0.577), float3(-0.577,0.577,-0.577), float3(-0.577,0.577,0.577),
    // Bottom face corners
    float3(-0.577,-0.577,-0.577), float3(0.577,-0.577,-0.577), float3(0.577,-0.577,0.577),
    float3(0.577,-0.577,0.577), float3(-0.577,-0.577,0.577), float3(-0.577,-0.577,-0.577),
};

// Per-draw data accessed via buffer device address. Normals are transformed
// by modelView; exact for uniform-scale transforms (inverse-transpose
// differs only by a factor that normalize removes). Non-uniform scale
// needs a proper normal matrix (M17).
struct DrawData
{
    float4x4 modelView;
    float4 lightDirView;
    float4 baseColor;
    float4 shadowColor;
    // Optional middle color of the diffuse ramp. Alpha == 0 means the
    // midpoint of shadowColor and baseColor.
    float4 midColor;
    float4 params; // x: bandCount, y: rimStrength, z: outline width in view units, w: capability bits
    // Reusable material capabilities (combined, not seven separate shaders):
    float4 emissive;  // rgb + intensity (engine cores, weapon fire)
    float4 fresnel;   // rgb + strength (shield rim, atmosphere gradient)
    float4 material;  // x: facet, y: shadow strength, z: attenuation, w: detail
    float4 highlight; // rgb: fourth ramp stop, a: banded threshold (0 = unused)
};

struct PushConstants
{
    float4x4 mvp;
    uint64_t drawDataAddress;
    uint64_t vertexAddress; // 0 = built-in cube; otherwise float4 pos/normal/outline per vertex
    uint mode; // 0 = toon, 1 = outline
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
};

// Anti-aliased step: softens band/specular/rim edges to avoid pixel crawl.
float aastep(float threshold, float value)
{
    float afwidth = fwidth(value) * 0.7071;
    return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VSOutput output;
    uint idx = min(vertexIndex, 35u);
    float3 objectPos = kCubePositions[idx];
    float3 objectNormal = kCubeNormals[idx];
    float3 outlineNormal = kCubeOutlineNormals[idx];

    // Optional caller-owned mesh (three float4 per vertex: position, normal,
    // baked smooth outline normal). Zero address keeps the built-in cube.
    if (pc.vertexAddress != 0u && vertexIndex < pc.vertexCount)
    {
        uint64_t base = pc.vertexAddress + uint64_t(vertexIndex) * 48u;
        typedef vk::BufferPointer<float4> Float4Ptr;
        Float4Ptr pp = { base };
        Float4Ptr np = { base + 16u };
        Float4Ptr op = { base + 32u };
        objectPos = pp.Get().xyz;
        objectNormal = np.Get().xyz;
        outlineNormal = op.Get().xyz;
    }

    DrawDataPtr drawPtr = { pc.drawDataAddress };
    DrawData draw = drawPtr.Get();

    if (pc.mode > 0)
    {
        // Outline extrusion along smoothed outline normals. params.z is a
        // CPU-computed object-space width derived from the desired
        // screen-pixel thickness (exact for rigid/uniform transforms).
        objectPos += outlineNormal * draw.params.z;
    }

    float4 viewPos = mul(draw.modelView, float4(objectPos, 1.0));
    float3 viewNormal = normalize(mul(draw.modelView, float4(objectNormal, 0.0)).xyz);

    output.position = mul(pc.mvp, float4(objectPos, 1.0));
    output.normalView = viewNormal;
    output.positionView = viewPos.xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    DrawDataPtr drawPtr = { pc.drawDataAddress };
    DrawData draw = drawPtr.Get();

    if (pc.mode > 0)
    {
        return float4(0.03, 0.03, 0.05, 1.0);
    }

    float3 normal = normalize(input.normalView);
    float3 lightDir = normalize(draw.lightDirView.xyz);
    float3 viewDir = normalize(-input.positionView);

    float ndotl = max(dot(normal, lightDir), 0.0);

    // Anti-aliased cel bands. Differentiate the continuous band coordinate
    // (not frac()) so the AA window stays monotonic at band boundaries.
    float bandCount = max(draw.params.x, 2.0);
    float rawBand = ndotl * bandCount;
    float bandWidth = fwidth(rawBand) * 0.7071;
    float nearestBand = round(rawBand);
    float steppedBand = nearestBand - 1.0 + smoothstep(-bandWidth, bandWidth, rawBand - nearestBand);
    float banded = clamp(steppedBand / max(bandCount - 1.0, 1.0), 0.0, 1.0);

    // Three-stop diffuse ramp (shadow -> mid -> lit). A zero alpha on the
    // middle color means "use the midpoint of shadow and lit colors", so
    // existing two-color callers keep their current look.
    float3 mid = draw.midColor.rgb;
    if (draw.midColor.w == 0.0)
    {
        mid = lerp(draw.shadowColor.rgb, draw.baseColor.rgb, 0.5);
    }
    float3 diffuse = banded < 0.5
        ? lerp(draw.shadowColor.rgb, mid, banded * 2.0)
        : lerp(mid, draw.baseColor.rgb, (banded - 0.5) * 2.0);

    // Stepped specular, masked so highlights never appear in shadow.
    float3 halfVec = normalize(lightDir + viewDir);
    float ndoth = max(dot(normal, halfVec), 0.0);
    float spec = aastep(0.65, pow(ndoth, 32.0)) * 0.7 * aastep(0.05, ndotl);

    // Rim light: brightens silhouettes facing away from the camera.
    float rim = aastep(0.6, pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0)) * draw.params.y;

    float3 color = diffuse + (spec + rim) * draw.baseColor.rgb;

    // Fourth ramp stop (authored palettes; placeholders can use it too).
    if (draw.highlight.w > 0.0)
    {
        color = lerp(color, draw.highlight.rgb,
                     smoothstep(draw.highlight.w - 0.15, draw.highlight.w + 0.15, banded));
    }

    uint caps = uint(draw.params.w + 0.5);
    float facing = 1.0 - max(dot(normal, viewDir), 0.0);

    // Emissive tint: engine exhaust, glowing cores, weapon fire/impact.
    if ((caps & 1u) != 0u)
    {
        color += draw.emissive.rgb * draw.emissive.w;
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
        color = lerp(color, draw.shadowColor.rgb,
                     draw.material.y * (1.0 - aastep(0.25, ndotl)));
        color *= lerp(1.0, 0.85 + 0.15 * sin(input.positionView.x * draw.material.w * 10.0),
                      step(0.001, draw.material.w));
    }

    return float4(color, draw.baseColor.a);
}
