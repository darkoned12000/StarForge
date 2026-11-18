#include "sun_renderer.hpp"

#include "core/log.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_mesh.hpp"
#include "spatial/camera_relative.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::environment
{

namespace
{

struct alignas(16) Vertex
{
    float pos[4];  // xyz: RTC position offset, w: 1.0f
    float norm[4]; // xyz: unit surface normal, w: 0.0f
    float uv[2];   // uv coordinates
    float pad[2];  // 48-byte stride
};
static_assert(sizeof(Vertex) == 48, "Vertex must be 48 bytes (std430 compatible)");

struct alignas(16) CorePushConstants
{
    math::Mat4f viewProj{1.0f};           // 64 bytes (offset 0)
    math::Vec4f patchOrigin{0.0f};         // 16 bytes (offset 64): xyz = camRel patch origin
    math::Vec4f coreColor{1.0f};           // 16 bytes (offset 80)
    math::Vec4f limbColor{1.0f};           // 16 bytes (offset 96)
    std::uint64_t vertexBufferAddress = 0; // 8 bytes  (offset 112)
    float limbDarkeningCoeff = 0.6f;      // 4 bytes  (offset 120)
    float time = 0.0f;                    // 4 bytes  (offset 124)
};
static_assert(sizeof(CorePushConstants) == 128, "CorePushConstants must be 128 bytes");
static_assert(offsetof(CorePushConstants, patchOrigin) == 64, "patchOrigin offset must be 64");
static_assert(offsetof(CorePushConstants, coreColor) == 80, "coreColor offset must be 80");
static_assert(offsetof(CorePushConstants, limbColor) == 96, "limbColor offset must be 96");
static_assert(offsetof(CorePushConstants, vertexBufferAddress) == 112, "vertexBufferAddress offset must be 112");
static_assert(offsetof(CorePushConstants, limbDarkeningCoeff) == 120, "limbDarkeningCoeff offset must be 120");
static_assert(offsetof(CorePushConstants, time) == 124, "time offset must be 124");

struct alignas(16) CoronaPushConstants
{
    math::Mat4f viewProj{1.0f};           // 64 bytes (offset 0)
    math::Vec4f patchOrigin{0.0f};         // 16 bytes (offset 64): xyz = camRel sun center, w = sun core radius
    math::Vec4f coronaColor{1.0f};         // 16 bytes (offset 80)
    std::uint64_t vertexBufferAddress = 0; // 8 bytes  (offset 96)
    float coronaRadius = 0.0f;            // 4 bytes  (offset 104)
    float time = 0.0f;                    // 4 bytes  (offset 108)
    float intensity = 1.0f;               // 4 bytes  (offset 112)
    float pad = 0.0f;                     // 4 bytes  (offset 116)
};
static_assert(sizeof(CoronaPushConstants) == 128, "CoronaPushConstants must be 128 bytes");
static_assert(offsetof(CoronaPushConstants, patchOrigin) == 64, "patchOrigin offset must be 64");
static_assert(offsetof(CoronaPushConstants, coronaColor) == 80, "coronaColor offset must be 80");
static_assert(offsetof(CoronaPushConstants, vertexBufferAddress) == 96, "vertexBufferAddress offset must be 96");
static_assert(offsetof(CoronaPushConstants, coronaRadius) == 104, "coronaRadius offset must be 104");
static_assert(offsetof(CoronaPushConstants, time) == 108, "time offset must be 108");
static_assert(offsetof(CoronaPushConstants, intensity) == 112, "intensity offset must be 112");

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
    constexpr std::size_t kSpirvHeaderBytes = 20;
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    if (spv.size() < kSpirvHeaderBytes || (spv.size() % sizeof(std::uint32_t)) != 0)
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

} // namespace

std::expected<SunRenderer, std::string> SunRenderer::create(const CreateInfo& info)
{
    if (info.instance == VK_NULL_HANDLE)
    {
        return std::unexpected("SunRenderer: invalid instance");
    }
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("SunRenderer: invalid device");
    }
    if (info.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("SunRenderer: invalid physical device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("SunRenderer: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.depthFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("SunRenderer: depthFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("SunRenderer: shaderDirectory is empty");
    }

    SunRenderer renderer;
    renderer.device_ = info.device;
    renderer.subdivisions_ = std::max(info.subdivisions, 2u);

    // Create GPU allocator
    auto allocator = gpu_resources::Allocator::create({
        .instance = info.instance,
        .physicalDevice = info.physicalDevice,
        .device = info.device,
    });
    if (!allocator)
    {
        return std::unexpected(std::move(allocator.error()));
    }
    renderer.allocator_ = std::make_unique<gpu_resources::Allocator>(std::move(*allocator));

    // Pipeline Layouts (Push Constants: 128 bytes)
    VkPushConstantRange corePushRange{};
    corePushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    corePushRange.offset = 0;
    corePushRange.size = static_cast<std::uint32_t>(sizeof(CorePushConstants));

    VkPipelineLayoutCreateInfo coreLayoutInfo{};
    coreLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    coreLayoutInfo.pushConstantRangeCount = 1;
    coreLayoutInfo.pPushConstantRanges = &corePushRange;

    if (const VkResult result = vkCreatePipelineLayout(info.device, &coreLayoutInfo, nullptr, &renderer.corePipelineLayout_);
        result != VK_SUCCESS)
    {
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout for SunCore", result));
    }

    VkPushConstantRange coronaPushRange{};
    coronaPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    coronaPushRange.offset = 0;
    coronaPushRange.size = static_cast<std::uint32_t>(sizeof(CoronaPushConstants));

    VkPipelineLayoutCreateInfo coronaLayoutInfo{};
    coronaLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    coronaLayoutInfo.pushConstantRangeCount = 1;
    coronaLayoutInfo.pPushConstantRanges = &coronaPushRange;

    if (const VkResult result = vkCreatePipelineLayout(info.device, &coronaLayoutInfo, nullptr, &renderer.coronaPipelineLayout_);
        result != VK_SUCCESS)
    {
        renderer.destroy();
        return std::unexpected(vulkan::describeFailure("vkCreatePipelineLayout for SunCorona", result));
    }

    // Load Shaders
    auto coreVert = loadShaderModule(info.device, info.shaderDirectory + "/sun.vert.spv");
    if (!coreVert)
    {
        renderer.destroy();
        return std::unexpected(coreVert.error());
    }
    auto coreFrag = loadShaderModule(info.device, info.shaderDirectory + "/sun.frag.spv");
    if (!coreFrag)
    {
        vkDestroyShaderModule(info.device, *coreVert, nullptr);
        renderer.destroy();
        return std::unexpected(coreFrag.error());
    }

    auto coronaVert = loadShaderModule(info.device, info.shaderDirectory + "/sun_corona.vert.spv");
    if (!coronaVert)
    {
        vkDestroyShaderModule(info.device, *coreVert, nullptr);
        vkDestroyShaderModule(info.device, *coreFrag, nullptr);
        renderer.destroy();
        return std::unexpected(coronaVert.error());
    }
    auto coronaFrag = loadShaderModule(info.device, info.shaderDirectory + "/sun_corona.frag.spv");
    if (!coronaFrag)
    {
        vkDestroyShaderModule(info.device, *coreVert, nullptr);
        vkDestroyShaderModule(info.device, *coreFrag, nullptr);
        vkDestroyShaderModule(info.device, *coronaVert, nullptr);
        renderer.destroy();
        return std::unexpected(coronaFrag.error());
    }

    // Create Core Pipeline (opaque, writes depth, backface culled)
    auto corePipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *coreVert,
        .fragmentShader = *coreFrag,
        .pipelineLayout = renderer.corePipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = true,
        .depthCompare = VK_COMPARE_OP_LESS,
        .backfaceCulling = true,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });

    // Create Corona Pipeline (additive blend, tests depth, no depth write, double-sided)
    auto coronaPipeline = vulkan::GraphicsPipeline::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .vertexShader = *coronaVert,
        .fragmentShader = *coronaFrag,
        .pipelineLayout = renderer.coronaPipelineLayout_,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depthTest = true,
        .depthWrite = false,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .backfaceCulling = false,
        .blendEnable = true,
        .blendSrcFactor = VK_BLEND_FACTOR_ONE,
        .blendDstFactor = VK_BLEND_FACTOR_ONE,
        .viewportWidth = 1.0f,
        .viewportHeight = 1.0f,
    });

    vkDestroyShaderModule(info.device, *coreVert, nullptr);
    vkDestroyShaderModule(info.device, *coreFrag, nullptr);
    vkDestroyShaderModule(info.device, *coronaVert, nullptr);
    vkDestroyShaderModule(info.device, *coronaFrag, nullptr);

    if (!corePipeline)
    {
        renderer.destroy();
        return std::unexpected(corePipeline.error());
    }
    if (!coronaPipeline)
    {
        renderer.destroy();
        return std::unexpected(coronaPipeline.error());
    }

    renderer.corePipeline_ = std::move(*corePipeline);
    renderer.coronaPipeline_ = std::move(*coronaPipeline);

    // Build geometry for default radius
    const double radius = info.defaultRadius > 0.0 ? info.defaultRadius : 10000.0;
    if (auto res = renderer.buildGeometry(radius, renderer.subdivisions_); !res)
    {
        renderer.destroy();
        return std::unexpected(res.error());
    }

    core::logInfo("SunRenderer", "Initialized visitable celestial sun renderer (subdivisions={}, radius={}m)",
                  renderer.subdivisions_, radius);

    return renderer;
}

SunRenderer::~SunRenderer()
{
    destroy();
}

SunRenderer::SunRenderer(SunRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      corePipelineLayout_(std::exchange(other.corePipelineLayout_, VK_NULL_HANDLE)),
      coronaPipelineLayout_(std::exchange(other.coronaPipelineLayout_, VK_NULL_HANDLE)),
      corePipeline_(std::exchange(other.corePipeline_, std::nullopt)),
      coronaPipeline_(std::exchange(other.coronaPipeline_, std::nullopt)),
      allocator_(std::exchange(other.allocator_, nullptr)),
      coreVertexBuffer_(std::exchange(other.coreVertexBuffer_, nullptr)),
      coreIndexBuffer_(std::exchange(other.coreIndexBuffer_, nullptr)),
      coronaVertexBuffer_(std::exchange(other.coronaVertexBuffer_, nullptr)),
      coronaIndexBuffer_(std::exchange(other.coronaIndexBuffer_, nullptr)),
      currentRadius_(other.currentRadius_),
      subdivisions_(other.subdivisions_),
      patchVertexCount_(other.patchVertexCount_),
      patchIndexCount_(other.patchIndexCount_),
      facePatches_(std::move(other.facePatches_))
{
}

SunRenderer& SunRenderer::operator=(SunRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        corePipelineLayout_ = std::exchange(other.corePipelineLayout_, VK_NULL_HANDLE);
        coronaPipelineLayout_ = std::exchange(other.coronaPipelineLayout_, VK_NULL_HANDLE);
        corePipeline_ = std::exchange(other.corePipeline_, std::nullopt);
        coronaPipeline_ = std::exchange(other.coronaPipeline_, std::nullopt);
        allocator_ = std::exchange(other.allocator_, nullptr);
        coreVertexBuffer_ = std::exchange(other.coreVertexBuffer_, nullptr);
        coreIndexBuffer_ = std::exchange(other.coreIndexBuffer_, nullptr);
        coronaVertexBuffer_ = std::exchange(other.coronaVertexBuffer_, nullptr);
        coronaIndexBuffer_ = std::exchange(other.coronaIndexBuffer_, nullptr);
        currentRadius_ = other.currentRadius_;
        subdivisions_ = other.subdivisions_;
        patchVertexCount_ = other.patchVertexCount_;
        patchIndexCount_ = other.patchIndexCount_;
        facePatches_ = std::move(other.facePatches_);
    }
    return *this;
}

void SunRenderer::destroy() noexcept
{
    corePipeline_.reset();
    coronaPipeline_.reset();
    coreVertexBuffer_.reset();
    coreIndexBuffer_.reset();
    coronaVertexBuffer_.reset();
    coronaIndexBuffer_.reset();
    allocator_.reset();

    if (device_ != VK_NULL_HANDLE)
    {
        if (corePipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, corePipelineLayout_, nullptr);
            corePipelineLayout_ = VK_NULL_HANDLE;
        }
        if (coronaPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, coronaPipelineLayout_, nullptr);
            coronaPipelineLayout_ = VK_NULL_HANDLE;
        }
        device_ = VK_NULL_HANDLE;
    }
}

std::expected<void, std::string> SunRenderer::buildGeometry(double radius, std::uint32_t subdivisions)
{
    currentRadius_ = radius;
    subdivisions_ = subdivisions;

    constexpr std::array<planets::CubeFace, 6> kFaces = {
        planets::CubeFace::PositiveX, planets::CubeFace::NegativeX,
        planets::CubeFace::PositiveY, planets::CubeFace::NegativeY,
        planets::CubeFace::PositiveZ, planets::CubeFace::NegativeZ,
    };

    facePatches_.clear();
    facePatches_.reserve(6);

    const std::uint32_t vertsPerSide = subdivisions + 1;
    patchVertexCount_ = vertsPerSide * vertsPerSide;
    patchIndexCount_ = subdivisions * subdivisions * 6;

    const std::size_t totalVertices = patchVertexCount_ * 6;
    const std::size_t totalIndices = patchIndexCount_; // each face reuses the same index topology

    std::vector<Vertex> coreVertices(totalVertices);
    std::vector<Vertex> coronaVertices(totalVertices);
    std::vector<std::uint32_t> sharedIndices;

    const double coronaRadius = radius * 6.0;

    for (std::size_t f = 0; f < 6; ++f)
    {
        planets::PlanetPatch patch;
        patch.face = kFaces[f];
        patch.uMin = -1.0;
        patch.uMax = 1.0;
        patch.vMin = -1.0;
        patch.vMax = 1.0;
        // Patch center in double precision
        const math::Vec3d centerDir = planets::cubeToSphere(patch.face, 0.0, 0.0);
        patch.center = centerDir * radius;
        facePatches_.push_back(patch);

        // Core mesh (RTC from patch.center)
        const auto coreMesh = planets::generatePatchMesh(patch, subdivisions, radius);
        if (f == 0)
        {
            sharedIndices = coreMesh.indices;
        }

        const std::size_t baseVert = f * patchVertexCount_;
        for (std::size_t v = 0; v < patchVertexCount_; ++v)
        {
            coreVertices[baseVert + v].pos[0] = coreMesh.positions[v].x;
            coreVertices[baseVert + v].pos[1] = coreMesh.positions[v].y;
            coreVertices[baseVert + v].pos[2] = coreMesh.positions[v].z;
            coreVertices[baseVert + v].pos[3] = 1.0f;

            coreVertices[baseVert + v].norm[0] = coreMesh.normals[v].x;
            coreVertices[baseVert + v].norm[1] = coreMesh.normals[v].y;
            coreVertices[baseVert + v].norm[2] = coreMesh.normals[v].z;
            coreVertices[baseVert + v].norm[3] = 0.0f;

            coreVertices[baseVert + v].uv[0] = coreMesh.uvs[v].x;
            coreVertices[baseVert + v].uv[1] = coreMesh.uvs[v].y;
            coreVertices[baseVert + v].pad[0] = 0.0f;
            coreVertices[baseVert + v].pad[1] = 0.0f;
        }

        // Corona mesh (RTC relative to sun center, radius = coronaRadius)
        planets::PlanetPatch coronaPatch = patch;
        coronaPatch.center = math::Vec3d{0.0, 0.0, 0.0};
        const auto coronaMesh = planets::generatePatchMesh(coronaPatch, subdivisions, coronaRadius);
        for (std::size_t v = 0; v < patchVertexCount_; ++v)
        {
            coronaVertices[baseVert + v].pos[0] = coronaMesh.positions[v].x;
            coronaVertices[baseVert + v].pos[1] = coronaMesh.positions[v].y;
            coronaVertices[baseVert + v].pos[2] = coronaMesh.positions[v].z;
            coronaVertices[baseVert + v].pos[3] = 1.0f;

            coronaVertices[baseVert + v].norm[0] = coronaMesh.normals[v].x;
            coronaVertices[baseVert + v].norm[1] = coronaMesh.normals[v].y;
            coronaVertices[baseVert + v].norm[2] = coronaMesh.normals[v].z;
            coronaVertices[baseVert + v].norm[3] = 0.0f;

            coronaVertices[baseVert + v].uv[0] = coronaMesh.uvs[v].x;
            coronaVertices[baseVert + v].uv[1] = coronaMesh.uvs[v].y;
            coronaVertices[baseVert + v].pad[0] = 0.0f;
            coronaVertices[baseVert + v].pad[1] = 0.0f;
        }
    }

    // Allocate Buffers
    const VkDeviceSize vertexBytes = totalVertices * sizeof(Vertex);
    const VkDeviceSize indexBytes = totalIndices * sizeof(std::uint32_t);

    auto coreVBuf = gpu_resources::Buffer::create(*allocator_, {
        .size = vertexBytes,
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.SunCore.VertexBuffer",
    });
    if (!coreVBuf) return std::unexpected(coreVBuf.error());

    auto coreIBuf = gpu_resources::Buffer::create(*allocator_, {
        .size = indexBytes,
        .usage = gpu_resources::BufferUsage::Index,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.SunCore.IndexBuffer",
    });
    if (!coreIBuf) return std::unexpected(coreIBuf.error());

    auto coronaVBuf = gpu_resources::Buffer::create(*allocator_, {
        .size = vertexBytes,
        .usage = gpu_resources::BufferUsage::Storage,
        .memory = gpu_resources::MemoryPlacement::CpuToGpu,
        .name = "starforge.SunCorona.VertexBuffer",
    });
    if (!coronaVBuf) return std::unexpected(coronaVBuf.error());

    // Upload data
    auto mapAndCopy = [](gpu_resources::Buffer& buf, const void* data, std::size_t bytes) -> std::expected<void, std::string> {
        auto mapped = buf.map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, data, bytes);
        buf.unmap();
        return {};
    };

    if (auto r = mapAndCopy(*coreVBuf, coreVertices.data(), vertexBytes); !r) return r;
    if (auto r = mapAndCopy(*coreIBuf, sharedIndices.data(), indexBytes); !r) return r;
    if (auto r = mapAndCopy(*coronaVBuf, coronaVertices.data(), vertexBytes); !r) return r;

    coreVertexBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*coreVBuf));
    coreIndexBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*coreIBuf));
    coronaVertexBuffer_ = std::make_unique<gpu_resources::Buffer>(std::move(*coronaVBuf));

    return {};
}

void SunRenderer::render(VkCommandBuffer cmd, const SunRenderContext& context, const SunParams& params)
{
    if (!corePipeline_ || !coronaPipeline_ || !coreVertexBuffer_ || !coreIndexBuffer_)
    {
        return;
    }

    // Regenerate geometry if radius changed significantly
    if (std::abs(params.radius - currentRadius_) > 1.0)
    {
        if (auto r = buildGeometry(params.radius, subdivisions_); !r)
        {
            core::logError("SunRenderer", "Failed to update sun geometry: {}", r.error());
            return;
        }
    }

    const float renderTime = params.time != 0.0f ? params.time : context.time;

    // -------------------------------------------------------------
    // Pass 1: Opaque Sun Core (Limb darkening, writes depth)
    // -------------------------------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, corePipeline_->handle());
    vkCmdBindIndexBuffer(cmd, coreIndexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const VkDeviceAddress coreBaseAddr = coreVertexBuffer_->deviceAddress(device_);

    for (std::size_t i = 0; i < facePatches_.size(); ++i)
    {
        const auto setup = spatial::makeLargeObjectSetup(
            params.worldPosition, context.cameraPosition, facePatches_[i].center);

        CorePushConstants corePush;
        corePush.viewProj = context.viewProj;
        const auto camRel = spatial::toFloat32(setup.cameraRelativeOrigin);
        corePush.patchOrigin = math::Vec4f{camRel.x, camRel.y, camRel.z, 0.0f};
        corePush.coreColor = params.coreColor;
        corePush.limbColor = params.limbColor;
        corePush.vertexBufferAddress = coreBaseAddr + i * patchVertexCount_ * sizeof(Vertex);
        corePush.limbDarkeningCoeff = params.limbDarkeningCoeff;
        corePush.time = renderTime;

        vkCmdPushConstants(cmd, corePipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(CorePushConstants), &corePush);

        vkCmdDrawIndexed(cmd, patchIndexCount_, 1, 0, 0, 0);
    }

    // -------------------------------------------------------------
    // Pass 2: Additive Corona Shell (Flares, depth-tested behind disk)
    // -------------------------------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, coronaPipeline_->handle());
    vkCmdBindIndexBuffer(cmd, coreIndexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const math::Vec3d camToSun = spatial::toCameraRelative(params.worldPosition, context.cameraPosition);
    const math::Vec3f camToSunF = spatial::toFloat32(camToSun);
    const VkDeviceAddress coronaBaseAddr = coronaVertexBuffer_->deviceAddress(device_);
    const double coronaRadius = params.radius * params.coronaRadiusMultiplier;

    for (std::size_t i = 0; i < facePatches_.size(); ++i)
    {
        CoronaPushConstants coronaPush;
        coronaPush.viewProj = context.viewProj;
        coronaPush.patchOrigin = math::Vec4f{camToSunF.x, camToSunF.y, camToSunF.z, static_cast<float>(params.radius)};
        coronaPush.coronaColor = params.coronaColor;
        coronaPush.vertexBufferAddress = coronaBaseAddr + i * patchVertexCount_ * sizeof(Vertex);
        coronaPush.coronaRadius = static_cast<float>(coronaRadius);
        coronaPush.time = renderTime;
        coronaPush.intensity = params.coronaIntensity;
        coronaPush.pad = 0.0f;

        vkCmdPushConstants(cmd, coronaPipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(CoronaPushConstants), &coronaPush);

        vkCmdDrawIndexed(cmd, patchIndexCount_, 1, 0, 0, 0);
    }
}

} // namespace starforge::environment
