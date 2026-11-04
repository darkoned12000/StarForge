// Cube with per-face colors and asymmetric triangle.
// Arrays are static const to avoid stack spills.

static const float3 kCubePositions[36] = {
    // Front face (z = +0.5): red
    float3(-0.5,-0.5, 0.5), float3(0.5,-0.5,0.5), float3(0.5,0.5,0.5),
    float3(0.5,0.5,0.5), float3(-0.5,0.5,0.5), float3(-0.5,-0.5,0.5),
    // Back face (z = -0.5): green
    float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5), float3(-0.5,0.5,-0.5),
    float3(-0.5,0.5,-0.5), float3(0.5,0.5,-0.5), float3(0.5,-0.5,-0.5),
    // Left face (x = -0.5): blue
    float3(-0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5), float3(-0.5,0.5,0.5),
    float3(-0.5,0.5,0.5), float3(-0.5,0.5,-0.5), float3(-0.5,-0.5,-0.5),
    // Right face (x = +0.5): yellow
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(0.5,0.5,0.5), float3(0.5,-0.5,0.5),
    // Top face (y = +0.5): magenta
    float3(-0.5,0.5,0.5), float3(0.5,0.5,0.5), float3(0.5,0.5,-0.5),
    float3(0.5,0.5,-0.5), float3(-0.5,0.5,-0.5), float3(-0.5,0.5,0.5),
    // Bottom face (y = -0.5): cyan
    float3(-0.5,-0.5,-0.5), float3(0.5,-0.5,-0.5), float3(0.5,-0.5,0.5),
    float3(0.5,-0.5,0.5), float3(-0.5,-0.5,0.5), float3(-0.5,-0.5,-0.5),
};

static const float3 kCubeColors[36] = {
    // Front: red
    float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),
    // Back: green
    float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),
    // Left: blue
    float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),
    // Right: yellow
    float3(1,1,0),float3(1,1,0),float3(1,1,0),float3(1,1,0),float3(1,1,0),float3(1,1,0),
    // Top: magenta
    float3(1,0,1),float3(1,0,1),float3(1,0,1),float3(1,0,1),float3(1,0,1),float3(1,0,1),
    // Bottom: cyan
    float3(0,1,1),float3(0,1,1),float3(0,1,1),float3(0,1,1),float3(0,1,1),float3(0,1,1),
};

static const float3 kTrianglePositions[3] = {
    float3(-0.7, -0.5, 0.0),
    float3( 0.3, -0.5, 0.0),
    float3( 0.1,  0.7, 0.0),
};

static const float3 kTriangleColors[3] = {
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0),
};

struct PushConstants
{
    float4x4 mvp;
    uint drawType; // 0 = cube (36 vertices), 1 = triangle (3 vertices)
};

[[vk::push_constant]] PushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VSOutput out;

    if (pc.drawType == 0)
    {
        // Clamp to prevent out-of-bounds reads if vertex count exceeds 36.
        uint idx = min(vertexIndex, 35u);
        out.position = mul(pc.mvp, float4(kCubePositions[idx], 1.0));
        out.color = kCubeColors[idx];
    }
    else
    {
        uint idx = vertexIndex % 3;
        out.position = mul(pc.mvp, float4(kTrianglePositions[idx], 1.0));
        out.color = kTriangleColors[idx];
    }

    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
