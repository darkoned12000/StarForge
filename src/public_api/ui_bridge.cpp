#include "ui_bridge.hpp"

#include "environment/ui_renderer.hpp"
#include "gameplay/ui_shapes.hpp"

#include <array>
#include <utility>
#include <vector>

namespace starforge::public_api_bridge
{

struct RawUiCanvas::Impl
{
    environment::UiRenderer renderer;
    std::vector<RawUiQuad> quads;
    std::vector<environment::UiVertex> vertices;
};

RawUiCanvas::~RawUiCanvas() = default;
RawUiCanvas::RawUiCanvas(RawUiCanvas&&) noexcept = default;
RawUiCanvas& RawUiCanvas::operator=(RawUiCanvas&&) noexcept = default;

std::expected<std::unique_ptr<RawUiCanvas>, std::string> RawUiCanvas::create(const CreateInfo& info)
{
    auto renderer = environment::UiRenderer::create({
        .instance = info.instance, .device = info.device,
        .physicalDevice = info.physicalDevice, .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory});
    if (!renderer) return std::unexpected(renderer.error());
    auto result = std::unique_ptr<RawUiCanvas>(new RawUiCanvas());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*renderer)});
    return result;
}

void RawUiCanvas::clear() noexcept
{
    if (impl_)
    {
        impl_->quads.clear();
        impl_->vertices.clear();
    }
}

void RawUiCanvas::addRect(float x, float y, float w, float h, const float color[4])
{
    if (!impl_) return;
    RawUiQuad q;
    q.x0 = x; q.y0 = y; q.x1 = x + w; q.y1 = y + h;
    for (int i = 0; i < 4; ++i) q.color[i] = color[i];
    impl_->quads.push_back(q);
}

void RawUiCanvas::addPanel(float x, float y, float w, float h, float outlineWidth,
                           const RawUiColors& colors)
{
    if (!impl_) return;
    const float* palette[3] = {colors.fill, colors.outline, colors.accent};
    for (const auto& sq : gameplay::ui_shapes::panelQuads(x, y, w, h, outlineWidth))
    {
        RawUiQuad q;
        q.x0 = sq.quad.x0; q.y0 = sq.quad.y0; q.x1 = sq.quad.x1; q.y1 = sq.quad.y1;
        const float* c = palette[sq.colorIndex];
        for (int i = 0; i < 4; ++i) q.color[i] = c[i];
        impl_->quads.push_back(q);
    }
}

void RawUiCanvas::addBar(float x, float y, float w, float h, float value,
                         const float fill[4], const float track[4])
{
    if (!impl_) return;
    const float* palette[2] = {fill, track};
    for (const auto& sq : gameplay::ui_shapes::barQuads(x, y, w, h, value))
    {
        RawUiQuad q;
        q.x0 = sq.quad.x0; q.y0 = sq.quad.y0; q.x1 = sq.quad.x1; q.y1 = sq.quad.y1;
        const float* c = palette[sq.colorIndex];
        for (int i = 0; i < 4; ++i) q.color[i] = c[i];
        impl_->quads.push_back(q);
    }
}

void RawUiCanvas::addText(const RawUiTextDraw& draw)
{
    if (!impl_) return;
    for (const auto& gq : gameplay::ui_text::textQuads(draw.text, draw.x, draw.y, draw.scale))
    {
        RawUiQuad q;
        q.x0 = gq.x0; q.y0 = gq.y0; q.x1 = gq.x1; q.y1 = gq.y1;
        for (int i = 0; i < 4; ++i) q.color[i] = draw.color[i];
        impl_->quads.push_back(q);
    }
}

void RawUiCanvas::render(VkCommandBuffer cmd, float width, float height)
{
    if (!impl_ || width <= 0.0f || height <= 0.0f) return;

    auto& vertices = impl_->vertices;
    vertices.clear();
    vertices.reserve(impl_->quads.size() * 6);
    const auto ndc = [width, height](float px, float py) -> std::array<float, 4> {
        return {2.0f * px / width - 1.0f, 1.0f - 2.0f * py / height, 0.0f, 1.0f};
    };
    for (const auto& q : impl_->quads)
    {
        const std::array<float, 4> c{q.color[0], q.color[1], q.color[2], q.color[3]};
        const auto a = ndc(q.x0, q.y0), b = ndc(q.x1, q.y0);
        const auto d = ndc(q.x1, q.y1), e = ndc(q.x0, q.y1);
        vertices.push_back({a, c});
        vertices.push_back({b, c});
        vertices.push_back({d, c});
        vertices.push_back({a, c});
        vertices.push_back({d, c});
        vertices.push_back({e, c});
    }
    impl_->renderer.render(cmd, vertices);
}

std::size_t RawUiCanvas::vertexCount() const noexcept
{
    return impl_ ? impl_->quads.size() * 6 : 0u;
}

std::expected<void, std::string> RawUiCanvas::recreate(const CreateInfo& info)
{
    if (!impl_) return std::unexpected("RawUiCanvas is moved-from");
    auto renderer = environment::UiRenderer::create({
        .instance = info.instance, .device = info.device,
        .physicalDevice = info.physicalDevice, .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory});
    if (!renderer) return std::unexpected(renderer.error());
    impl_->renderer = std::move(*renderer);
    return {};
}

} // namespace starforge::public_api_bridge
