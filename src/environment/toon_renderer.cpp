#include "toon_renderer.hpp"

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "internal/effect_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

// Per-draw data for toon.slang. Keep this layout identical to the shader's
// DrawData struct: the model view, light, and color ramp live device-side
// so the push-constant block stays below Vulkan's guaranteed 128-byte range.
struct alignas(16) DrawData
{
    math::Mat4f modelView{1.0f};
    math::Vec4f lightDirView{0.0f, 0.0f, 1.0f, 0.0f};
    math::Vec4f baseColor{1.0f, 0.25f, 0.2f, 1.0f};
    math::Vec4f shadowColor{0.25f, 0.05f, 0.1f, 1.0f};
    // Alpha == 0 means the midpoint of shadowColor and baseColor.
    math::Vec4f midColor{0.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f params{3.0f, 0.5f, 0.02f, 0.0f};
    math::Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f fresnel{0.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f material{0.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4f highlight{0.0f, 0.0f, 0.0f, 0.0f};
    std::uint64_t textureAddress = 0;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::uint64_t emissiveTextureAddress = 0;
    std::uint32_t emissiveTextureWidth = 0;
    std::uint32_t emissiveTextureHeight = 0;
};

static_assert(sizeof(DrawData) == 240, "Toon DrawData must match the shader layout (240 bytes)");
static_assert(offsetof(DrawData, params) == 128, "Toon DrawData params offset must be 128");

// Push constants for toon.slang: clip matrix + BDA pointer + mode. 80 bytes.
struct PushConstants
{
    math::Mat4f mvp{1.0f};
    std::uint64_t drawDataAddress = 0;
    std::uint64_t vertexAddress = 0;
    std::uint32_t mode = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t pad0 = 0;
    std::uint32_t pad1 = 0;
};

static_assert(sizeof(PushConstants) == 96, "Toon push constants must be 96 bytes");
static_assert(offsetof(PushConstants, drawDataAddress) == 64, "Toon draw data address offset must be 64");
static_assert(offsetof(PushConstants, vertexAddress) == 72, "Toon vertex address offset must be 72");

constexpr std::size_t kDrawDataStride = 256;
constexpr std::uint32_t kDefaultMaxDrawsPerFrame = 512;



} // namespace

std::expected<ToonRenderer, std::string> ToonRenderer::create(const CreateInfo& info)
{
    if (info.instance == VK_NULL_HANDLE)
    {
        return std::unexpected("ToonRenderer: invalid instance");
    }
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ToonRenderer: invalid device");
    }
    if (info.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("ToonRenderer: invalid physical device");
    }
    if (info.framesInFlight == 0)
    {
        return std::unexpected("ToonRenderer: framesInFlight must be at least 1");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("ToonRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("ToonRenderer: shaderDirectory is empty");
    }

    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(info.physicalDevice, &deviceProps);
    if (deviceProps.limits.maxPushConstantsSize < sizeof(PushConstants))
    {
        return std::unexpected("ToonRenderer: device push-constant range is smaller than 96 bytes");
    }

    VkPhysicalDeviceFeatures2 deviceFeatures{};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    deviceFeatures.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(info.physicalDevice, &deviceFeatures);
    if (!features12.bufferDeviceAddress || !deviceFeatures.features.shaderInt64)
    {
        return std::unexpected("ToonRenderer requires bufferDeviceAddress and shaderInt64");
    }

    ToonRenderer renderer;
    renderer.device_ = info.device;
    renderer.framesInFlight_ = info.framesInFlight;
    renderer.maxDrawsPerFrame_ = kDefaultMaxDrawsPerFrame;

    // Host-visible per-draw buffer. Each frame gets its own slot so the GPU
    // never reads data that the CPU is about to overwrite on the next frame.
    auto allocator = gpu_resources::Allocator::create({
        .instance = info.instance,
        .physicalDevice = info.physicalDevice,
        .device = info.device,
    });
    if (!allocator)
    {
        return std::unexpected(std::move(allocator.error()));
    }
    auto drawBuffer = gpu_resources::Buffer::create(*allocator, {
        .size = static_cast<VkDeviceSize>(kDrawDataStride * renderer.maxDrawsPerFrame_ * info.framesInFlight),
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.Toon.DrawData",
    });
    if (!drawBuffer)
    {
        return std::unexpected(std::move(drawBuffer.error()));
    }
    renderer.allocator_ = std::make_shared<gpu_resources::Allocator>(std::move(*allocator));
    renderer.drawDataBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*drawBuffer));
    auto mappedDrawData = renderer.drawDataBuffer_->map();
    if (!mappedDrawData)
    {
        return std::unexpected(mappedDrawData.error());
    }
    renderer.mappedDrawData_ = *mappedDrawData;

    auto vertModule = internal::loadShaderModule(info.device, info.shaderDirectory + "/toon.vert.spv");
    if (!vertModule)
    {
        return std::unexpected(vertModule.error());
    }
    auto fragModule = internal::loadShaderModule(info.device, info.shaderDirectory + "/toon.frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        return std::unexpected(fragModule.error());
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<std::uint32_t>(sizeof(PushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult result = vkCreatePipelineLayout(info.device, &layoutInfo, nullptr, &layout);
        result != VK_SUCCESS)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        vkDestroyShaderModule(info.device, *fragModule, nullptr);
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout", result));
    }
    renderer.pipelineLayout_ = layout;

    auto toon = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = true,
        .specializationInfo = info.specializationInfo,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    auto outline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        // The outline pass runs after the base pass and must not write
        // depth, so expanded back faces cannot poke through recesses.
        .depthWrite = false,
        // Screen-space outline keeps the base depth, so equal depth must pass.
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .backfaceCulling = false,
        .cullFrontFace = true,
        .specializationInfo = info.specializationInfo,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });

    auto fail = [&](const std::string& message) {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        vkDestroyShaderModule(info.device, *fragModule, nullptr);
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(message);
    };
    if (!toon) return fail(toon.error());
    if (!outline) return fail(outline.error());
    auto emissive = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *vertModule,
        .fragmentShader = *fragModule,
        .pipelineLayout = renderer.pipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .specializationInfo = info.specializationInfo,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    if (!emissive) return fail(emissive.error());
    renderer.toonPipeline_ = std::move(*toon);
    renderer.outlinePipeline_ = std::move(*outline);
    renderer.emissivePipeline_ = std::move(*emissive);

    // Shader modules are consumed by pipeline creation.
    vkDestroyShaderModule(info.device, *vertModule, nullptr);
    vkDestroyShaderModule(info.device, *fragModule, nullptr);
    return renderer;
}

ToonRenderer::~ToonRenderer()
{
    destroy();
}

ToonRenderer::ToonRenderer(ToonRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      framesInFlight_(std::exchange(other.framesInFlight_, 1)),
      maxDrawsPerFrame_(std::exchange(other.maxDrawsPerFrame_, 512)),
      drawsInFrame_(std::exchange(other.drawsInFrame_, 0)),
      currentDrawSlot_(std::exchange(other.currentDrawSlot_, 0)),
      allocator_(std::move(other.allocator_)),
      drawDataBuffer_(std::move(other.drawDataBuffer_)),
      mappedDrawData_(std::exchange(other.mappedDrawData_, nullptr)),
      toonPipeline_(std::exchange(other.toonPipeline_, std::nullopt)),
      outlinePipeline_(std::exchange(other.outlinePipeline_, std::nullopt)),
      emissivePipeline_(std::exchange(other.emissivePipeline_, std::nullopt))
{
}

ToonRenderer& ToonRenderer::operator=(ToonRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        framesInFlight_ = std::exchange(other.framesInFlight_, 1);
        maxDrawsPerFrame_ = std::exchange(other.maxDrawsPerFrame_, 512);
        drawsInFrame_ = std::exchange(other.drawsInFrame_, 0);
        currentDrawSlot_ = std::exchange(other.currentDrawSlot_, 0);
        allocator_ = std::move(other.allocator_);
        drawDataBuffer_ = std::move(other.drawDataBuffer_);
        mappedDrawData_ = std::exchange(other.mappedDrawData_, nullptr);
        toonPipeline_ = std::exchange(other.toonPipeline_, std::nullopt);
        outlinePipeline_ = std::exchange(other.outlinePipeline_, std::nullopt);
        emissivePipeline_ = std::exchange(other.emissivePipeline_, std::nullopt);
    }
    return *this;
}

void ToonRenderer::destroy() noexcept
{
    emissivePipeline_.reset();
    outlinePipeline_.reset();
    toonPipeline_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    drawDataBuffer_.reset();
    mappedDrawData_ = nullptr;
    allocator_.reset();
    device_ = VK_NULL_HANDLE;
    framesInFlight_ = 2;
    maxDrawsPerFrame_ = 512;
    drawsInFrame_ = 0;
    currentDrawSlot_ = 0;
}

ToonMesh::ToonMesh(std::shared_ptr<gpu_resources::Allocator> allocator,
                   std::unique_ptr<gpu_resources::Buffer> buffer, std::uint64_t address,
                   std::uint32_t count) noexcept
    : allocator_(std::move(allocator)), buffer_(std::move(buffer)), address_(address), vertexCount_(count)
{
}

ToonMesh::~ToonMesh() = default;
ToonMesh::ToonMesh(ToonMesh&&) noexcept = default;
ToonMesh& ToonMesh::operator=(ToonMesh&&) noexcept = default;

std::expected<ToonMesh, std::string> ToonRenderer::createMesh(std::span<const ToonVertex> vertices)
{
    if (!allocator_ || vertices.empty())
    {
        return std::unexpected("ToonRenderer::createMesh requires a renderer and non-empty vertices");
    }

    const std::size_t size = vertices.size() * 48;
    std::vector<char> host(size);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        auto* out = reinterpret_cast<float*>(host.data() + i * 48);
        out[0] = vertices[i].position.x;
        out[1] = vertices[i].position.y;
        out[2] = vertices[i].position.z;
        out[4] = vertices[i].normal.x;
        out[5] = vertices[i].normal.y;
        out[6] = vertices[i].normal.z;
        out[8] = vertices[i].outlineNormal.x;
        out[9] = vertices[i].outlineNormal.y;
        out[10] = vertices[i].outlineNormal.z;
    }

    auto buffer = gpu_resources::Buffer::create(*allocator_, {
        .size = static_cast<VkDeviceSize>(size),
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.Toon.Mesh",
    });
    if (!buffer) return std::unexpected(buffer.error());

    {
        auto mapped = buffer->map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, host.data(), size);
        (void)buffer->flush(0, size);
        buffer->unmap();
    }
    const std::uint64_t address = buffer->deviceAddress(device_);
    return ToonMesh(allocator_, std::make_unique<gpu_resources::Buffer>(std::move(*buffer)), address,
                    static_cast<std::uint32_t>(vertices.size()));
}

std::expected<ToonMesh, std::string> ToonRenderer::createTexturedMesh(
    std::span<const ToonTexturedVertex> vertices, const std::uint8_t* rgba,
    std::uint32_t width, std::uint32_t height,
    const std::uint8_t* emissiveRgba, std::uint32_t emissiveWidth, std::uint32_t emissiveHeight)
{
    if (!allocator_ || vertices.empty()) return std::unexpected("Textured toon mesh requires vertices");
    if ((width == 0 || height == 0) || rgba == nullptr)
        return std::unexpected("Textured toon mesh requires an RGBA base-colour texture");

    std::vector<char> host(vertices.size() * 80);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        auto* out = reinterpret_cast<float*>(host.data() + i * 80);
        const auto& v = vertices[i];
        out[0] = v.position.x; out[1] = v.position.y; out[2] = v.position.z;
        out[4] = v.normal.x; out[5] = v.normal.y; out[6] = v.normal.z;
        out[8] = v.outlineNormal.x; out[9] = v.outlineNormal.y; out[10] = v.outlineNormal.z;
        out[12] = v.uv.x; out[13] = v.uv.y;
        out[16] = v.tangent.x; out[17] = v.tangent.y; out[18] = v.tangent.z; out[19] = v.tangent.w;
    }

    auto buffer = gpu_resources::Buffer::create(*allocator_, {
        .size = static_cast<VkDeviceSize>(host.size()),
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.Toon.TexturedMesh",
    });
    if (!buffer) return std::unexpected(buffer.error());
    {
        auto mapped = buffer->map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, host.data(), host.size());
        (void)buffer->flush(0, host.size());
        buffer->unmap();
    }
    const std::uint64_t address = buffer->deviceAddress(device_);

    // One packed RGBA8 uint32 per texel (4 bytes, not 16).
    const std::size_t texelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint32_t> texels(texelCount);
    for (std::size_t i = 0; i < texelCount; ++i)
    {
        texels[i] = static_cast<std::uint32_t>(rgba[i * 4 + 0]) |
                    (static_cast<std::uint32_t>(rgba[i * 4 + 1]) << 8) |
                    (static_cast<std::uint32_t>(rgba[i * 4 + 2]) << 16) |
                    (static_cast<std::uint32_t>(rgba[i * 4 + 3]) << 24);
    }
    auto texture = gpu_resources::Buffer::create(*allocator_, {
        .size = static_cast<VkDeviceSize>(texels.size() * sizeof(std::uint32_t)),
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.Toon.BaseColorTexture",
    });
    if (!texture) return std::unexpected(texture.error());
    {
        auto mapped = texture->map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, texels.data(), texels.size() * sizeof(std::uint32_t));
        (void)texture->flush(0, texels.size() * sizeof(std::uint32_t));
        texture->unmap();
    }

    ToonMesh mesh(allocator_, std::make_unique<gpu_resources::Buffer>(std::move(*buffer)), address,
                  static_cast<std::uint32_t>(vertices.size()));
    mesh.textureBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*texture));
    mesh.textureAddress_ = mesh.textureBuffer_->deviceAddress(device_);
    mesh.textureWidth_ = width;
    mesh.textureHeight_ = height;

    if (emissiveRgba != nullptr && emissiveWidth > 0 && emissiveHeight > 0)
    {
        const std::size_t emTexelCount = static_cast<std::size_t>(emissiveWidth) * emissiveHeight;
        std::vector<std::uint32_t> emTexels(emTexelCount);
        for (std::size_t i = 0; i < emTexelCount; ++i)
        {
            emTexels[i] = static_cast<std::uint32_t>(emissiveRgba[i * 4 + 0]) |
                          (static_cast<std::uint32_t>(emissiveRgba[i * 4 + 1]) << 8) |
                          (static_cast<std::uint32_t>(emissiveRgba[i * 4 + 2]) << 16) |
                          (static_cast<std::uint32_t>(emissiveRgba[i * 4 + 3]) << 24);
        }
        auto emTexture = gpu_resources::Buffer::create(*allocator_, {
            .size = static_cast<VkDeviceSize>(emTexels.size() * sizeof(std::uint32_t)),
            .usage = gpu_resources::BufferUsage::Storage,
            .memory = gpu_resources::MemoryPlacement::CpuToGpu,
            .name = "starforge.Toon.EmissiveTexture",
        });
        if (!emTexture) return std::unexpected(emTexture.error());
        {
            auto mapped = emTexture->map();
            if (!mapped) return std::unexpected(mapped.error());
            std::memcpy(*mapped, emTexels.data(), emTexels.size() * sizeof(std::uint32_t));
            (void)emTexture->flush(0, emTexels.size() * sizeof(std::uint32_t));
            emTexture->unmap();
        }
        mesh.emissiveTextureBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*emTexture));
        mesh.emissiveTextureAddress_ = mesh.emissiveTextureBuffer_->deviceAddress(device_);
        mesh.emissiveTextureWidth_ = emissiveWidth;
        mesh.emissiveTextureHeight_ = emissiveHeight;
    }
    return mesh;
}

void ToonRenderer::beginFrame() noexcept
{
    drawsInFrame_ = 0;
}

void ToonRenderer::renderMesh(VkCommandBuffer cmd, const ToonRenderContext& context,
                              const ToonParams& params, const ToonMesh& mesh)
{
    if (!toonPipeline_.has_value() || !outlinePipeline_.has_value() ||
        !emissivePipeline_.has_value() || !drawDataBuffer_)
    {
        return;
    }

    DrawData drawData;
    drawData.modelView = context.modelView;
    drawData.lightDirView = math::Vec4f{context.lightDirectionView, 0.0f};
    drawData.baseColor = params.baseColor;
    drawData.shadowColor = params.shadowColor;
    drawData.midColor = params.midColor;
    drawData.emissive = params.emissiveColor;
    drawData.fresnel = params.fresnelColor;
    drawData.material = params.materialParams;
    drawData.highlight = params.highlightColor;
    drawData.textureAddress = mesh.textureAddress();
    drawData.textureWidth = mesh.textureWidth();
    drawData.textureHeight = mesh.textureHeight();
    drawData.emissiveTextureAddress = mesh.emissiveTextureAddress();
    drawData.emissiveTextureWidth = mesh.emissiveTextureWidth();
    drawData.emissiveTextureHeight = mesh.emissiveTextureHeight();

    // Outline width is now in screen pixels; the shader offsets clip XY by
    // this amount using the viewport passed through push-constant padding.
    drawData.params = math::Vec4f{std::max(params.bandCount, 2.0f), params.rimStrength,
                                   params.outlineWidth, static_cast<float>(params.capabilities)};

    if (drawsInFrame_ >= maxDrawsPerFrame_)
    {
        // Rare overflow: grow per-frame draw capacity. Wait for idle before
        // replacing the buffer because previous frames may still read it.
        if (device_ != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device_);
        }
        const std::uint32_t newMax = std::max(maxDrawsPerFrame_ * 2u, drawsInFrame_ + 1u);
        auto newBuffer = gpu_resources::Buffer::create(*allocator_, {
            .size = static_cast<VkDeviceSize>(kDrawDataStride * newMax * std::max(1u, framesInFlight_)),
            .usage = gpu_resources::BufferUsage::Storage,
            .memory = gpu_resources::MemoryPlacement::CpuToGpu,
            .name = "starforge.Toon.DrawData",
        });
        if (newBuffer)
        {
            auto mapped = newBuffer->map();
            if (mapped)
            {
                drawDataBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*newBuffer));
                mappedDrawData_ = *mapped;
                maxDrawsPerFrame_ = newMax;
                currentDrawSlot_ = 0;
                drawsInFrame_ = 0;
            }
        }
    }

    // Monotonic slot cursor over framesInFlight * maxDrawsPerFrame_ slots.
    // beginFrame() resets drawsInFrame_; callers that do not call beginFrame
    // still get safe growth on overflow.
    const std::uint32_t totalSlots = std::max(1u, framesInFlight_) * std::max(1u, maxDrawsPerFrame_);
    const std::uint32_t slot = currentDrawSlot_ % totalSlots;
    ++currentDrawSlot_;
    ++drawsInFrame_;
    const VkDeviceSize slotOffset = static_cast<VkDeviceSize>(slot) * kDrawDataStride;
    if (mappedDrawData_ != nullptr)
    {
        auto* destination = static_cast<char*>(mappedDrawData_) + slotOffset;
        std::memcpy(destination, &drawData, sizeof(DrawData));
        (void)drawDataBuffer_->flush(slotOffset, sizeof(DrawData));
    }

    PushConstants push;
    push.mvp = context.viewProj;
    push.drawDataAddress = drawDataBuffer_->deviceAddress(device_) + slotOffset;
    push.vertexAddress = mesh.deviceAddress();
    push.vertexCount = mesh.vertexCount();
    // Screen-space outline needs the viewport size; smuggle it through the
    // free padding as bit-cast floats (no push-constant range growth).
    push.pad0 = std::bit_cast<std::uint32_t>(
        context.resolution.x > 0.0f ? context.resolution.x : 1.0f);
    push.pad1 = std::bit_cast<std::uint32_t>(
        context.resolution.y > 0.0f ? context.resolution.y : 1.0f);

    const std::uint32_t vertexCount = push.vertexCount > 0 ? push.vertexCount : 36u;
    const bool emissive = (params.capabilities & 1u) != 0;

    // Base pass first: writes depth, so the outline pass only appears beyond
    // the silhouette. Emissive materials use the additive glow pipeline.
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    (emissive ? *emissivePipeline_ : *toonPipeline_).bindAndDraw(cmd, vertexCount, 0, 1);

    if (!emissive)
    {
        push.mode = 1;
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        outlinePipeline_->bindAndDraw(cmd, vertexCount, 0, 1);
    }
}

void ToonRenderer::render(VkCommandBuffer cmd, const ToonRenderContext& context, const ToonParams& params)
{
    renderMesh(cmd, context, params, ToonMesh{});
}

} // namespace starforge::environment
