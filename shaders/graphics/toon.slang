// Toon (cel-shaded) cube: quantized diffuse bands, specular, and rim light.
// The same shader drives the inverted-hull outline pass (mode = 1): the
// vertex stage expands positions along view-space normals by a CPU-computed
// view-space width (constant screen-pixel thickness) and the fragment stage
// outputs a flat dark color. Geometry is a static cube (36 vertices,
// per-face normals) until real mesh assets land (M17).
//
// Push constants carry both the clip matrix (mvp) and the view matrix
// (modelView) so lighting happens in view space while positions land in
// clip space. 192 bytes total; creation requires the device to report at
// least that much push-constant space.

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

// 192 bytes: mvp (64) + modelView (64) + lightDirView (16) + baseColor (16) +
// shadowColor (16) + params (16). Normals are transformed by modelView;
// exact for uniform-scale transforms (inverse-transpose differs only by a
// factor that normalize removes). Non-uniform scale needs a proper normal
// matrix (M17).
struct PushConstants
{
    float4x4 mvp;
    float4x4 modelView;
    float4 lightDirView;
    float4 baseColor;
    float4 shadowColor;
    float4 params; // x: bandCount, y: rimStrength, z: outline width in view units, w: mode (0 = toon, 1 = outline)
};

[[vk::push_constant]] PushConstants pc;

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

    if (pc.params.w > 0.5)
    {
        // Outline extrusion along smoothed outline normals. params.z is a
        // CPU-computed object-space width derived from the desired
        // screen-pixel thickness (exact for rigid/uniform transforms).
        objectPos += kCubeOutlineNormals[idx] * pc.params.z;
    }

    float4 viewPos = mul(pc.modelView, float4(objectPos, 1.0));
    float3 viewNormal = normalize(mul(pc.modelView, float4(objectNormal, 0.0)).xyz);

    output.position = mul(pc.mvp, float4(objectPos, 1.0));
    output.normalView = viewNormal;
    output.positionView = viewPos.xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    if (pc.params.w > 0.5)
    {
        return float4(0.03, 0.03, 0.05, 1.0);
    }

    float3 normal = normalize(input.normalView);
    float3 lightDir = normalize(pc.lightDirView.xyz);
    float3 viewDir = normalize(-input.positionView);

    float ndotl = max(dot(normal, lightDir), 0.0);

    // Anti-aliased cel bands. Differentiate the continuous band coordinate
    // (not frac()) so the AA window stays monotonic at band boundaries.
    float bandCount = max(pc.params.x, 2.0);
    float rawBand = ndotl * bandCount;
    float bandWidth = fwidth(rawBand) * 0.7071;
    float nearestBand = round(rawBand);
    float steppedBand = nearestBand - 1.0 + smoothstep(-bandWidth, bandWidth, rawBand - nearestBand);
    float banded = clamp(steppedBand / max(bandCount - 1.0, 1.0), 0.0, 1.0);
    float3 diffuse = lerp(pc.shadowColor.rgb, pc.baseColor.rgb, banded);

    // Stepped specular, masked so highlights never appear in shadow.
    float3 halfVec = normalize(lightDir + viewDir);
    float ndoth = max(dot(normal, halfVec), 0.0);
    float spec = aastep(0.65, pow(ndoth, 32.0)) * 0.7 * aastep(0.05, ndotl);

    // Rim light: brightens silhouettes facing away from the camera.
    float rim = aastep(0.6, pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0)) * pc.params.y;

    float3 color = diffuse + (spec + rim) * pc.baseColor.rgb;
    return float4(color, pc.baseColor.a);
}
