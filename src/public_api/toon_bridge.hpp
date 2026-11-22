#pragma once

#include "bridge_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

// Internal bridge between the public toon facade and the environment
// backend. Uses only plain data and Vulkan handles so no translation unit
// ever includes both the public math header and the internal GLM-based
// math aliases (both live in starforge::math). This header must not be
// installed.
namespace starforge::public_api_bridge
{

struct RawToonContext
{
    RawMat4f viewProj;
    RawMat4f modelView;
    RawVec3f lightDirectionView;
    RawVec2f resolution;
};

struct RawToonParams
{
    RawVec4f baseColor;
    RawVec4f shadowColor;
    RawVec4f midColor; // alpha=0 means the midpoint of shadow/base
    float bandCount = 3.0f;
    float rimStrength = 0.5f;
    float outlineWidth = 2.0f;
    std::uint32_t capabilities = 0;
    RawVec4f emissiveColor;
    RawVec4f fresnelColor;
    RawVec4f materialParams;
    RawVec4f highlightColor;
};

struct RawToonVertex
{
    float position[3] = {};
    float normal[3] = {};
    float outlineNormal[3] = {};
};

struct RawToonTexturedVertex
{
    float position[3] = {};
    float normal[3] = {};
    float outlineNormal[3] = {};
    float uv[2] = {};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

// Opaque GPU mesh owned by RawToon. Must be destroyed before the renderer.
class RawToonMesh
{
public:
    RawToonMesh() = default;
    ~RawToonMesh();
    RawToonMesh(RawToonMesh&&) noexcept;
    RawToonMesh& operator=(RawToonMesh&&) noexcept;
    RawToonMesh(const RawToonMesh&) = delete;
    RawToonMesh& operator=(const RawToonMesh&) = delete;

    [[nodiscard]] std::uint32_t vertexCount() const noexcept { return vertexCount_; }

private:
    friend class RawToon;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t vertexCount_ = 0;
};

// Owns the internal toon renderer (pipelines, shader modules).
class RawToon
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        std::uint32_t framesInFlight = 2;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<RawToon>, std::string> create(const CreateInfo& info);

    ~RawToon();
    RawToon(const RawToon&) = delete;
    RawToon& operator=(const RawToon&) = delete;
    RawToon(RawToon&& other) noexcept;
    RawToon& operator=(RawToon&& other) noexcept;

    // Resets the per-frame draw bookkeeping (draw counter). Must be called
    // once per frame before any draw is recorded.
    void beginFrame() noexcept;

    void render(VkCommandBuffer cmd, const RawToonContext& context, const RawToonParams& params);

    [[nodiscard]] std::expected<std::unique_ptr<RawToonMesh>, std::string>
    createMesh(const RawToonVertex* vertices, std::uint32_t vertexCount);

    [[nodiscard]] std::expected<std::unique_ptr<RawToonMesh>, std::string>
    createTexturedMesh(const RawToonTexturedVertex* vertices, std::uint32_t vertexCount,
                       const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                       const std::uint8_t* emissiveRgba = nullptr,
                       std::uint32_t emissiveWidth = 0, std::uint32_t emissiveHeight = 0);

    void renderMesh(VkCommandBuffer cmd, const RawToonContext& context,
                    const RawToonParams& params, const RawToonMesh& mesh);

private:
    RawToon() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::public_api_bridge
