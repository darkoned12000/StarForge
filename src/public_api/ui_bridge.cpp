#include "ui_bridge.hpp"

#include "environment/ui_renderer.hpp"
#include "gameplay/ui_text.hpp"

#include <array>
#include <utility>
#include <vector>

namespace starforge::public_api_bridge
{

struct RawUiCanvas::Impl
{
    environment::UiRenderer renderer;
    std::vector<RawUiTextDraw> draws;
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
        impl_->draws.clear();
        impl_->vertices.clear();
    }
}

void RawUiCanvas::addText(const RawUiTextDraw& draw)
{
    if (impl_) impl_->draws.push_back(draw);
}

void RawUiCanvas::render(VkCommandBuffer cmd, float width, float height)
{
    if (!impl_ || width <= 0.0f || height <= 0.0f) return;

    namespace font = starforge::gameplay::ui_text;
    auto& vertices = impl_->vertices;
    vertices.clear();

    // Reserve: worst case 5x7 lit pixels x 6 verts per text draw.
    std::size_t estimate = 0;
    for (const auto& draw : impl_->draws)
        estimate += draw.text.size() * font::kGlyphWidth * font::kGlyphHeight * 6;
    vertices.reserve(estimate);

    const auto ndc = [width, height](float px, float py) -> std::array<float, 4> {
        return {2.0f * px / width - 1.0f, 1.0f - 2.0f * py / height, 0.0f, 1.0f};
    };

    for (const auto& draw : impl_->draws)
    {
        const std::array<float, 4> color{draw.color[0], draw.color[1], draw.color[2], draw.color[3]};
        for (const auto& q : font::textQuads(draw.text, draw.x, draw.y, draw.scale))
        {
            const auto a = ndc(q.x0, q.y0);
            const auto b = ndc(q.x1, q.y0);
            const auto c = ndc(q.x1, q.y1);
            const auto d = ndc(q.x0, q.y1);
            // Two triangles per lit pixel, wound counter-clockwise.
            vertices.push_back(environment::UiVertex{a, color});
            vertices.push_back(environment::UiVertex{b, color});
            vertices.push_back(environment::UiVertex{c, color});
            vertices.push_back(environment::UiVertex{a, color});
            vertices.push_back(environment::UiVertex{c, color});
            vertices.push_back(environment::UiVertex{d, color});
        }
    }
    impl_->renderer.render(cmd, vertices);
}

std::size_t RawUiCanvas::vertexCount() const noexcept
{
    return impl_ ? impl_->vertices.size() : 0u;
}

} // namespace starforge::public_api_bridge
