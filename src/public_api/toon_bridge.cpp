#include "toon_bridge.hpp"

#include "environment/toon_renderer.hpp"

#include <utility>
#include <vector>

namespace starforge::public_api_bridge
{
namespace
{

[[nodiscard]] ::starforge::math::Mat4f toInternalMat4(const RawMat4f& value) noexcept
{
    ::starforge::math::Mat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[static_cast<int>(column)][static_cast<int>(row)] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] environment::ToonRenderContext toInternalContext(const RawToonContext& value) noexcept
{
    environment::ToonRenderContext result;
    result.viewProj = toInternalMat4(value.viewProj);
    result.modelView = toInternalMat4(value.modelView);
    result.lightDirectionView = ::starforge::math::Vec3f{value.lightDirectionView.x, value.lightDirectionView.y,
                                                          value.lightDirectionView.z};
    result.resolution = ::starforge::math::Vec2f{value.resolution.x, value.resolution.y};
    return result;
}

[[nodiscard]] environment::ToonParams toInternalParams(const RawToonParams& value) noexcept
{
    environment::ToonParams result;
    result.baseColor = ::starforge::math::Vec4f{value.baseColor.x, value.baseColor.y, value.baseColor.z, value.baseColor.w};
    result.shadowColor = ::starforge::math::Vec4f{value.shadowColor.x, value.shadowColor.y, value.shadowColor.z,
                                                  value.shadowColor.w};
    result.midColor = ::starforge::math::Vec4f{value.midColor.x, value.midColor.y, value.midColor.z, value.midColor.w};
    result.bandCount = value.bandCount;
    result.rimStrength = value.rimStrength;
    result.outlineWidth = value.outlineWidth;
    result.capabilities = value.capabilities;
    result.emissiveColor = ::starforge::math::Vec4f{value.emissiveColor.x, value.emissiveColor.y,
                                                     value.emissiveColor.z, value.emissiveColor.w};
    result.fresnelColor = ::starforge::math::Vec4f{value.fresnelColor.x, value.fresnelColor.y,
                                                    value.fresnelColor.z, value.fresnelColor.w};
    result.materialParams = ::starforge::math::Vec4f{value.materialParams.x, value.materialParams.y,
                                                      value.materialParams.z, value.materialParams.w};
    result.highlightColor = ::starforge::math::Vec4f{value.highlightColor.x, value.highlightColor.y,
                                                      value.highlightColor.z, value.highlightColor.w};
    return result;
}

} // namespace

struct RawToon::Impl
{
    environment::ToonRenderer renderer;
};

RawToon::~RawToon() = default;
RawToon::RawToon(RawToon&& other) noexcept : impl_(std::move(other.impl_))
{
}
RawToon& RawToon::operator=(RawToon&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<std::unique_ptr<RawToon>, std::string> RawToon::create(const CreateInfo& info)
{
    auto backend = environment::ToonRenderer::create({
        .instance = info.instance,
        .device = info.device,
        .physicalDevice = info.physicalDevice,
        .queueFamily = info.queueFamily,
        .framesInFlight = info.framesInFlight,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .specializationInfo = info.specializationInfo,
    });
    if (!backend)
    {
        return std::unexpected(backend.error());
    }

    auto result = std::unique_ptr<RawToon>(new RawToon());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*backend)});
    return result;
}

void RawToon::render(VkCommandBuffer cmd, const RawToonContext& context, const RawToonParams& params)
{
    if (impl_)
    {
        impl_->renderer.render(cmd, toInternalContext(context), toInternalParams(params));
    }
}

struct RawToonMesh::Impl
{
    environment::ToonMesh mesh;
};

RawToonMesh::~RawToonMesh() = default;
RawToonMesh::RawToonMesh(RawToonMesh&&) noexcept = default;
RawToonMesh& RawToonMesh::operator=(RawToonMesh&&) noexcept = default;

std::expected<std::unique_ptr<RawToonMesh>, std::string>
RawToon::createMesh(const RawToonVertex* vertices, std::uint32_t vertexCount)
{
    if (!impl_ || vertices == nullptr || vertexCount == 0)
    {
        return std::unexpected("RawToon::createMesh requires a renderer and vertices");
    }
    std::vector<environment::ToonVertex> converted(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        converted[i].position = ::starforge::math::Vec3f{vertices[i].position[0], vertices[i].position[1],
                                                          vertices[i].position[2]};
        converted[i].normal = ::starforge::math::Vec3f{vertices[i].normal[0], vertices[i].normal[1],
                                                        vertices[i].normal[2]};
        converted[i].outlineNormal = ::starforge::math::Vec3f{
            vertices[i].outlineNormal[0], vertices[i].outlineNormal[1], vertices[i].outlineNormal[2]};
    }
    auto mesh = impl_->renderer.createMesh(converted);
    if (!mesh) return std::unexpected(mesh.error());

    auto result = std::unique_ptr<RawToonMesh>(new RawToonMesh());
    result->impl_ = std::make_unique<RawToonMesh::Impl>(RawToonMesh::Impl{std::move(*mesh)});
    result->vertexCount_ = vertexCount;
    return result;
}

std::expected<std::unique_ptr<RawToonMesh>, std::string>
RawToon::createTexturedMesh(const RawToonTexturedVertex* vertices, std::uint32_t vertexCount,
                            const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                            const std::uint8_t* emissiveRgba, std::uint32_t emissiveWidth, std::uint32_t emissiveHeight)
{
    if (!impl_ || vertices == nullptr || vertexCount == 0)
        return std::unexpected("RawToon::createTexturedMesh requires a renderer and vertices");
    std::vector<environment::ToonTexturedVertex> converted(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        converted[i].position = ::starforge::math::Vec3f{vertices[i].position[0], vertices[i].position[1],
                                                          vertices[i].position[2]};
        converted[i].normal = ::starforge::math::Vec3f{vertices[i].normal[0], vertices[i].normal[1],
                                                        vertices[i].normal[2]};
        converted[i].outlineNormal = ::starforge::math::Vec3f{
            vertices[i].outlineNormal[0], vertices[i].outlineNormal[1], vertices[i].outlineNormal[2]};
        converted[i].uv = ::starforge::math::Vec2f{vertices[i].uv[0], vertices[i].uv[1]};
        converted[i].tangent = ::starforge::math::Vec4f{vertices[i].tangent[0], vertices[i].tangent[1],
                                                         vertices[i].tangent[2], vertices[i].tangent[3]};
    }
    auto mesh = impl_->renderer.createTexturedMesh(converted, rgba, width, height,
                                                   emissiveRgba, emissiveWidth, emissiveHeight);
    if (!mesh) return std::unexpected(mesh.error());
    auto result = std::unique_ptr<RawToonMesh>(new RawToonMesh());
    result->impl_ = std::make_unique<RawToonMesh::Impl>(RawToonMesh::Impl{std::move(*mesh)});
    result->vertexCount_ = vertexCount;
    return result;
}

void RawToon::renderMesh(VkCommandBuffer cmd, const RawToonContext& context,
                         const RawToonParams& params, const RawToonMesh& mesh)
{
    if (impl_ && mesh.impl_)
    {
        impl_->renderer.renderMesh(cmd, toInternalContext(context), toInternalParams(params),
                                   mesh.impl_->mesh);
    }
}

} // namespace starforge::public_api_bridge
