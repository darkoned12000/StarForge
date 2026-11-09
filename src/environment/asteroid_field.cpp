#include "asteroid_field.hpp"

#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_result.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

std::uint32_t hash32(std::uint32_t x)
{
    x = x * 1664525u + 1013904223u;
    x ^= x >> 16u;
    return x;
}

std::vector<char> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file)
    {
        return {};
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

std::expected<VkShaderModule, std::string> loadShaderModule(VkDevice device, const std::string& path)
{
    const auto spv = readFile(path);
    if (spv.empty())
    {
        return std::unexpected("Cannot read shader file: " + path);
    }
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    if (spv.size() < 20 || (spv.size() % sizeof(std::uint32_t)) != 0)
    {
        return std::unexpected("Corrupt SPIR-V binary: invalid byte count in " + path);
    }
    std::uint32_t magic = 0;
    std::memcpy(&magic, spv.data(), sizeof(magic));
    if (magic != kSpirvMagic)
    {
        return std::unexpected("Corrupt SPIR-V binary: bad magic in " + path);
    }
    std::vector<std::uint32_t> aligned(spv.size() / sizeof(std::uint32_t));
    std::memcpy(aligned.data(), spv.data(), spv.size());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = aligned.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateShaderModule(device, &info, nullptr, &module);
        result != VK_SUCCESS)
    {
        return std::unexpected(vulkan::describeFailure("vkCreateShaderModule", result));
    }
    return module;
}

// Must match PushConstants in asteroid.slang (112 bytes).
struct alignas(16) PushConstants
{
    float viewProj[16] = {};
    std::uint64_t vertexBufferAddress = 0;
    std::uint64_t instanceBufferAddress = 0;
    float time = 0.0f;
    alignas(16) float sunDirection[3] = {1.0f, 0.5f, 0.3f};
    std::uint32_t pad = 0;
};

static_assert(std::is_standard_layout_v<PushConstants>, "Asteroid PushConstants must be standard layout");
static_assert(sizeof(PushConstants) == 112, "Asteroid PushConstants must be 112 bytes");
static_assert(offsetof(PushConstants, sunDirection) == 96, "Asteroid sun direction offset must be 96");
static_assert(offsetof(PushConstants, pad) == 108, "Asteroid pad offset must be 108");

} // namespace

AsteroidMesh generateAsteroidMesh(float radius, std::uint32_t latSegments, std::uint32_t lonSegments)
{
    AsteroidMesh mesh;
    if (radius <= 0.0f || latSegments == 0 || lonSegments == 0)
    {
        return mesh;
    }

    const std::uint32_t rows = latSegments + 1;
    const std::uint32_t cols = lonSegments + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(rows) * cols * 8);
    mesh.indices.reserve(static_cast<std::size_t>(latSegments) * lonSegments * 6);

    const double pi = std::acos(-1.0);
    for (std::uint32_t j = 0; j < rows; ++j)
    {
        const double theta = pi * static_cast<double>(j) / static_cast<double>(latSegments);
        const double sinT = std::sin(theta);
        const double cosT = std::cos(theta);
        for (std::uint32_t i = 0; i < cols; ++i)
        {
            const double phi = 2.0 * pi * static_cast<double>(i) / static_cast<double>(lonSegments);
            const auto nx = static_cast<float>(sinT * std::cos(phi));
            const auto ny = static_cast<float>(-cosT);
            const auto nz = static_cast<float>(sinT * std::sin(phi));

            // Deterministic displacement from the direction seed.
            // Components are shifted positive before the unsigned cast
            // (negative float -> uint32 conversion is undefined behavior).
            const auto uNx = static_cast<std::uint32_t>((nx + 2.0f) * 1000.0f);
            const auto uNy = static_cast<std::uint32_t>((ny + 2.0f) * 1000.0f);
            const auto uNz = static_cast<std::uint32_t>((nz + 2.0f) * 1000.0f);
            const std::uint32_t seed = hash32(uNx * 73856093u ^ uNy * 19349663u ^ uNz * 83492791u);
            const float noise = static_cast<float>(seed) / 4294967296.0f; // [0,1)
            const float scale = radius * (1.0f + (noise - 0.5f) * 0.5f);

            mesh.vertices.insert(mesh.vertices.end(),
                                 {nx * scale, ny * scale, nz * scale, 1.0f, nx, ny, nz, 0.0f});
        }
    }
    for (std::uint32_t j = 0; j < latSegments; ++j)
    {
        for (std::uint32_t i = 0; i < lonSegments; ++i)
        {
            const std::uint32_t a = j * cols + i;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + cols;
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    return mesh;
}

std::expected<AsteroidFieldRenderer, std::string> AsteroidFieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("AsteroidFieldRenderer: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("AsteroidFieldRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.depthFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("AsteroidFieldRenderer: depthFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("AsteroidFieldRenderer: shaderDirectory is empty");
    }

    AsteroidFieldRenderer renderer;
    renderer.device_ = info.device;

    auto vertModule = loadShaderModule(info.device, info.shaderDirectory + "/asteroid.vert.spv");
    if (!vertModule)
    {
        return std::unexpected(std::move(vertModule.error()));
    }
    auto fragModule = loadShaderModule(info.device, info.shaderDirectory + "/asteroid.frag.spv");
    if (!fragModule)
    {
        vkDestroyShaderModule(info.device, *vertModule, nullptr);
        return std::unexpected(std::move(fragModule.error()));
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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

    auto pipeline = vulkan::GraphicsPipeline::create({
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
        .backfaceCulling = false,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });
    vkDestroyShaderModule(info.device, *vertModule, nullptr);
    vkDestroyShaderModule(info.device, *fragModule, nullptr);
    if (!pipeline)
    {
        vkDestroyPipelineLayout(info.device, renderer.pipelineLayout_, nullptr);
        renderer.pipelineLayout_ = VK_NULL_HANDLE;
        return std::unexpected(std::move(pipeline.error()));
    }
    renderer.pipeline_ = std::move(*pipeline);

    return renderer;
}

AsteroidFieldRenderer::~AsteroidFieldRenderer()
{
    destroy();
}

AsteroidFieldRenderer::AsteroidFieldRenderer(AsteroidFieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt))
{
}

AsteroidFieldRenderer& AsteroidFieldRenderer::operator=(AsteroidFieldRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
    }
    return *this;
}

void AsteroidFieldRenderer::destroy() noexcept
{
    pipeline_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void AsteroidFieldRenderer::render(VkCommandBuffer cmd, const AsteroidFieldRenderContext& context, const AsteroidFieldParams& params)
{
    if (!pipeline_.has_value())
    {
        return;
    }

    PushConstants push{};
    std::memcpy(push.viewProj, glm::value_ptr(context.viewProj), sizeof(push.viewProj));
    push.vertexBufferAddress = params.vertexBufferAddress;
    push.instanceBufferAddress = params.instanceBufferAddress;
    push.time = params.time;
    push.sunDirection[0] = context.sunDirection.x;
    push.sunDirection[1] = context.sunDirection.y;
    push.sunDirection[2] = context.sunDirection.z;

    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, static_cast<std::uint32_t>(sizeof(push)), &push);

    // Caller must bind the index buffer before render() (params carry
    // addresses for the shader, not the VkBuffer handle).
    pipeline_->bindAndDraw(cmd, 0, params.indexCount, params.instanceCount);
}

} // namespace starforge::environment
