// G-buffer pass: encodes object ID in R and view-space normal in GBA.
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
    float3(-0.5,-0.5,-0.5), float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,0.5),
    float3(0.5,-0.5,0.5), float3(0.5,-0.5,-0.5), float3(-0.5,-0.5,-0.5),
};
static const float3 kNormals[36] = {
    float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),float3(0,0,1),
    float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),float3(0,0,-1),
    float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),float3(-1,0,0),
    float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),float3(1,0,0),
    float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),float3(0,1,0),
    float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),float3(0,-1,0),
};
struct Push
{
    float4x4 viewProj;
    float4x4 modelView;
    uint objectId;
    float pad0, pad1, pad2;
};
[[vk::push_constant]] Push pc;
struct VSOutput { float4 position : SV_Position; float3 normalView : NORMAL0; };

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    uint i = min(vertexIndex, 35u);
    VSOutput out;
    out.position = mul(pc.viewProj, float4(kCubePositions[i], 1.0));
    out.normalView = normalize(mul(pc.modelView, float4(kNormals[i], 0.0)).xyz);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(float(pc.objectId), normalize(input.normalView));
}
