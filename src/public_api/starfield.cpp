#include "starforge/starfield.hpp"

#include "renderer_impl.hpp"
#include "starfield_bridge.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;

[[nodiscard]] bridge::RawMat4f toRaw(const math::Mat4f& value) noexcept
{
    bridge::RawMat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] bridge::RawVec3f toRaw(const math::Vec3f& value) noexcept
{
    return bridge::RawVec3f{value.x(), value.y(), value.z()};
}

[[nodiscard]] bridge::RawVec2f toRaw(const math::Vec2f& value) noexcept
{
    return bridge::RawVec2f{value.x(), value.y()};
}

[[nodiscard]] bridge::RawVec4f toRaw(const math::Vec4f& value) noexcept
{
    return bridge::RawVec4f{value.x(), value.y(), value.z(), value.w()};
}

[[nodiscard]] bridge::RawStarfield::CreateInfo makeBackendInfo(Renderer& renderer, const std::string& shaderDirectory)
{
    return bridge::RawStarfield::CreateInfo{
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .queueFamily = RendererInternalAccess::queueFamily(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
    };
}

[[nodiscard]] std::filesystem::path executableDirectory()
{
#if defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path.parent_path();
#elif defined(_WIN32)
    std::array<char, 4096> buffer{};
    const auto length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length == 0 ? std::filesystem::path{} : std::filesystem::path{buffer.data()}.parent_path();
#else
    return {};
#endif
}

// Resolves the bundled shader directory without assuming the build tree
// layout: an explicit override wins, then the STARFORGE_SHADER_DIR
// environment variable, then directories relative to the running
// executable, then the compile-time build directory (dev fallback), then
// a plain "shaders" directory. Returns the first candidate containing the
// starfield vertex shader, or the compile-time default so load failures
// report a concrete path.
[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    if (!requested.empty())
    {
        return requested;
    }

    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("STARFORGE_SHADER_DIR"); env != nullptr && env[0] != '\0')
    {
        candidates.emplace_back(env);
    }
    if (const auto exeDir = executableDirectory(); !exeDir.empty())
    {
        candidates.push_back(exeDir / "shaders");
        candidates.push_back(exeDir / ".." / "shaders");
        candidates.push_back(exeDir / ".." / "share" / "starforge" / "shaders");
    }
    candidates.emplace_back(STARFORGE_SHADER_DIR);
    candidates.emplace_back("shaders");

    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::exists(candidate / "starfield.vert.spv", error))
        {
            return candidate.string();
        }
    }
    return STARFORGE_SHADER_DIR;
}

} // namespace

struct Starfield::Impl
{
    std::unique_ptr<bridge::RawStarfield> backend;
    std::string shaderDirectory;
};

Starfield::Starfield(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Starfield::~Starfield() = default;

Starfield::Starfield(Starfield&& other) noexcept : impl_(std::move(other.impl_))
{
}

Starfield& Starfield::operator=(Starfield&& other) noexcept
{
    impl_ = std::move(other.impl_);
    return *this;
}

std::expected<Starfield, std::string> Starfield::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Starfield::create failed: Renderer is invalid or moved-from");
    }

    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawStarfield::create(makeBackendInfo(renderer, shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }

    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Starfield(std::move(impl));
}

void Starfield::record(FrameContext& frameContext, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return;
    }

    bridge::RawStarfieldContext rawContext;
    rawContext.viewProj = toRaw(context.viewProj);
    rawContext.invViewProj = toRaw(context.invViewProj);
    rawContext.cameraPosition = toRaw(context.cameraPosition);

    const Size2D extent = frameContext.extent();
    const math::Vec2f resolution = (context.resolution.x() <= 0.0f || context.resolution.y() <= 0.0f)
                                       ? math::Vec2f{static_cast<float>(extent.width), static_cast<float>(extent.height)}
                                       : context.resolution;
    rawContext.resolution = toRaw(resolution);
    rawContext.time = context.time;

    bridge::RawStarfieldParams rawParams;
    rawParams.worldPosition = toRaw(params.worldPosition);
    rawParams.radius = params.radius;
    rawParams.intensity = params.intensity;
    rawParams.color = toRaw(params.color);
    rawParams.limbDarkeningCoeff = params.limbDarkeningCoeff;

    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext), rawContext, rawParams);
}

std::expected<Renderer::FrameResult, std::string>
Starfield::render(Renderer& renderer, const RenderContext& context, const Params& params)
{
    if (!impl_ || !impl_->backend)
    {
        return std::unexpected("Starfield is moved-from");
    }

    return renderer.renderFrame(context.clearColor, [this, &context, &params](FrameContext& frameContext) {
        record(frameContext, context, params);
    });
}

std::expected<void, std::string> Starfield::recreate(Renderer& renderer)
{
    if (!impl_)
    {
        return std::unexpected("Starfield is moved-from");
    }
    if (!RendererInternalAccess::isValid(renderer))
    {
        return std::unexpected("Starfield::recreate failed: Renderer is invalid or moved-from");
    }

    renderer.waitIdle();
    auto backend = bridge::RawStarfield::create(makeBackendInfo(renderer, impl_->shaderDirectory));
    if (!backend)
    {
        return std::unexpected(backend.error());
    }
    impl_->backend = std::move(*backend);
    return {};
}

} // namespace starforge
