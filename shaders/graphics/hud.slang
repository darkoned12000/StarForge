// Toon HUD: flat, outlined panels and a portrait frame with graphic accents.
// Drawn as a fullscreen pass; all geometry is procedural in pixel space.
[[vk::binding(0, 0)]] Texture2D<float4> dummyTexture;
[[vk::binding(1, 0)]] SamplerState dummySampler;

struct Push
{
    float4 panel;   // x, y, width, height (pixels)
    float4 portrait;// x, y, width, height (pixels)
    float4 panelColor;
    float4 outlineColor;
    float4 accentColor;
    float4 screen;  // width, height, unused, unused
    float4 barValues; // normalized readout bar lengths (shield / hull / cargo)
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

float rectMask(float2 p, float4 r)
{
    float2 lo = r.xy;
    float2 hi = r.xy + r.zw;
    return step(lo.x, p.x) * step(p.x, hi.x) * step(lo.y, p.y) * step(p.y, hi.y);
}

float borderMask(float2 p, float4 r, float width)
{
    return rectMask(p, r) * (1.0 - rectMask(p, float4(r.x + width, r.y + width,
                                                     max(r.z - 2.0 * width, 0.0),
                                                     max(r.w - 2.0 * width, 0.0))));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 p = input.uv * pc.screen.xy;
    float3 color = float3(0.0, 0.0, 0.0);
    float coverage = 0.0; // transparent everywhere except HUD elements

    float panel = rectMask(p, pc.panel);
    color = lerp(color, pc.panelColor.rgb, panel);
    coverage = max(coverage, panel);

    float outline = borderMask(p, pc.panel, 3.0);
    color = lerp(color, pc.outlineColor.rgb, outline);
    coverage = max(coverage, outline);

    float portrait = rectMask(p, pc.portrait);
    color = lerp(color, pc.panelColor.rgb * 0.5, portrait);
    coverage = max(coverage, portrait);
    float portraitOutline = borderMask(p, pc.portrait, 2.0);
    color = lerp(color, pc.accentColor.rgb, portraitOutline);
    coverage = max(coverage, portraitOutline);

    // Three graphic accent bars (health / shield / ammo).
    for (uint i = 0; i < 3u; ++i)
    {
        float4 bar = float4(pc.panel.x + 8.0,
                            pc.panel.y + pc.panel.w - 12.0 - float(i) * 7.0,
                            (pc.panel.z - 16.0) * pc.barValues[i],
                            3.0);
        float barMask = rectMask(p, bar);
        color = lerp(color, pc.accentColor.rgb, barMask);
        coverage = max(coverage, barMask);
    }
    // Alpha-blended overlay: only the elements modify the framebuffer.
    return float4(color, coverage);
}
