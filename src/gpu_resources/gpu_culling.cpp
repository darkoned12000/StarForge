#include "gpu_resources/gpu_culling.hpp"

#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace starforge::gpu_resources
{

std::expected<GpuCulling, std::string>
GpuCulling::create(Allocator& allocator, const CreateInfo& info)
{
    if (info.instanceCount == 0)
    {
        return std::unexpected("GpuCulling requires instanceCount > 0");
    }
    if (info.framesInFlight == 0)
    {
        return std::unexpected("GpuCulling requires framesInFlight > 0");
    }

    GpuCulling gc;
    gc.instanceCount_ = info.instanceCount;
    gc.framesInFlight_ = info.framesInFlight;

    // Bounds buffer: vec4 per instance (16 bytes each).
    {
        Buffer::CreateInfo bufInfo;
        bufInfo.size = info.instanceCount * 4 * sizeof(float);
        bufInfo.usage = BufferUsage::Storage;
        bufInfo.memory = MemoryPlacement::CpuToGpu;
        bufInfo.name = "InstanceBounds";

        auto result = Buffer::create(allocator, bufInfo);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        gc.boundsBuffer_ = std::move(*result);
    }

    // Double-buffered indirect command buffers (VkDrawIndirectCommand = 16 bytes).
    gc.indirectBuffers_.resize(info.framesInFlight);
    for (std::uint32_t i = 0; i < info.framesInFlight; ++i)
    {
        Buffer::CreateInfo bufInfo;
        bufInfo.size = sizeof(std::uint32_t) * 4; // vertexCount, instanceCount, firstVertex, firstInstance
        bufInfo.usage = BufferUsage::StorageIndirect;
        bufInfo.memory = MemoryPlacement::CpuToGpu;
        bufInfo.name = "IndirectCommand";

        auto result = Buffer::create(allocator, bufInfo);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        gc.indirectBuffers_[i] = std::move(*result);

        // Initialize the static part once: VkDrawIndirectCommand.vertexCount.
        // Only instanceCount is reset every frame (by vkCmdFillBuffer).
        auto mapped = gc.indirectBuffers_[i].map();
        if (!mapped)
        {
            return std::unexpected(mapped.error());
        }
        auto* command = static_cast<std::uint32_t*>(*mapped);
        command[0] = 36; // vertexCount (cube: 12 triangles)
        command[1] = 0;  // instanceCount
        command[2] = 0;  // firstVertex
        command[3] = 0;  // firstInstance
        (void)gc.indirectBuffers_[i].flush(0, VK_WHOLE_SIZE);
        gc.indirectBuffers_[i].unmap();
    }

    // Double-buffered visible indices buffers (uint32_t per instance).
    gc.visibleIndicesBuffers_.resize(info.framesInFlight);
    for (std::uint32_t i = 0; i < info.framesInFlight; ++i)
    {
        Buffer::CreateInfo bufInfo;
        bufInfo.size = info.instanceCount * sizeof(std::uint32_t);
        bufInfo.usage = BufferUsage::Storage;
        bufInfo.memory = MemoryPlacement::CpuToGpu;
        bufInfo.name = "VisibleIndices";

        auto result = Buffer::create(allocator, bufInfo);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        gc.visibleIndicesBuffers_[i] = std::move(*result);
    }

    return gc;
}

GpuCulling::~GpuCulling()
{
    destroy();
}

GpuCulling::GpuCulling(GpuCulling&& other) noexcept
    : instanceCount_(other.instanceCount_),
      framesInFlight_(other.framesInFlight_),
      boundsBuffer_(std::move(other.boundsBuffer_)),
      indirectBuffers_(std::move(other.indirectBuffers_)),
      visibleIndicesBuffers_(std::move(other.visibleIndicesBuffers_))
{
    other.instanceCount_ = 0;
    other.framesInFlight_ = 0;
}

GpuCulling& GpuCulling::operator=(GpuCulling&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        instanceCount_ = other.instanceCount_;
        framesInFlight_ = other.framesInFlight_;
        boundsBuffer_ = std::move(other.boundsBuffer_);
        indirectBuffers_ = std::move(other.indirectBuffers_);
        visibleIndicesBuffers_ = std::move(other.visibleIndicesBuffers_);
        other.instanceCount_ = 0;
        other.framesInFlight_ = 0;
    }
    return *this;
}

void GpuCulling::destroy() noexcept
{
    boundsBuffer_ = Buffer{};
    indirectBuffers_.clear();
    visibleIndicesBuffers_.clear();
    instanceCount_ = 0;
    framesInFlight_ = 0;
}

std::expected<void, std::string>
GpuCulling::uploadBounds(const std::vector<float>& boundsData)
{
    if (boundsData.size() != instanceCount_ * 4)
    {
        return std::unexpected("boundsData size must be instanceCount * 4");
    }

    auto mapped = boundsBuffer_.map();
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::memcpy(*mapped, boundsData.data(), boundsData.size() * sizeof(float));
    (void)boundsBuffer_.flush(0, VK_WHOLE_SIZE);
    boundsBuffer_.unmap();
    return {};
}

Buffer& GpuCulling::indirectBuffer(std::uint32_t frameIndex) noexcept
{
    return indirectBuffers_[frameIndex % framesInFlight_];
}

const Buffer& GpuCulling::indirectBuffer(std::uint32_t frameIndex) const noexcept
{
    return indirectBuffers_[frameIndex % framesInFlight_];
}

Buffer& GpuCulling::visibleIndicesBuffer(std::uint32_t frameIndex) noexcept
{
    return visibleIndicesBuffers_[frameIndex % framesInFlight_];
}

const Buffer& GpuCulling::visibleIndicesBuffer(std::uint32_t frameIndex) const noexcept
{
    return visibleIndicesBuffers_[frameIndex % framesInFlight_];
}

Buffer& GpuCulling::boundsBuffer() noexcept
{
    return boundsBuffer_;
}

const Buffer& GpuCulling::boundsBuffer() const noexcept
{
    return boundsBuffer_;
}

std::expected<void, std::string>
GpuCulling::resetIndirectBuffer(std::uint32_t frameIndex)
{
    auto& buf = indirectBuffer(frameIndex);
    auto mapped = buf.map();
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    // VkDrawIndirectCommand: vertexCount=36, instanceCount=0, firstVertex=0, firstInstance=0
    auto* data = static_cast<std::uint32_t*>(*mapped);
    data[0] = 36; // vertexCount
    data[1] = 0;  // instanceCount (reset to 0)
    data[2] = 0;  // firstVertex
    data[3] = 0;  // firstInstance
    (void)buf.flush(0, VK_WHOLE_SIZE);
    buf.unmap();
    return {};
}

std::expected<void, std::string>
GpuCulling::recordCulling(VkCommandBuffer cmd,
                          VkDevice /*device*/,
                          ComputePipeline& cullPipeline,
                          std::uint32_t frameIndex,
                          const float* frustumPlanes,
                          std::uint32_t totalInstanceCount)
{
    if (totalInstanceCount > instanceCount_)
    {
        return std::unexpected("totalInstanceCount exceeds allocated instance bounds");
    }
    if (frustumPlanes == nullptr)
    {
        return std::unexpected("frustumPlanes is null");
    }

    // Step 1: GPU-side reset of VkDrawIndirectCommand.instanceCount (byte offset
    // 4). vertexCount was initialized once at creation and is never touched.
    vkCmdFillBuffer(cmd, indirectBuffer(frameIndex).handle(),
                    sizeof(std::uint32_t), sizeof(std::uint32_t), 0u);

    // Step 2: TRANSFER_WRITE -> COMPUTE_SHADER_READ_WRITE for the indirect
    // command, plus host-written bounds -> COMPUTE_SHADER_READ.
    {
        VkBufferMemoryBarrier2 barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barriers[0].buffer = indirectBuffer(frameIndex).handle();
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        barriers[1].buffer = boundsBuffer().handle();
        barriers[1].size = VK_WHOLE_SIZE;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 2;
        dep.pBufferMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    // Step 3: push constants + inline dispatch into the caller's command
    // buffer. No submit/wait here — the caller records the draw afterwards.
    struct PushData
    {
        float planes[24];
        std::uint32_t totalInstanceCount;
        std::uint32_t padding;
    } pushData{};
    std::memcpy(pushData.planes, frustumPlanes, sizeof(pushData.planes));
    pushData.totalInstanceCount = totalInstanceCount;

    const Buffer* storageBuffers[] = {
        &boundsBuffer(),
        &indirectBuffer(frameIndex),
        &visibleIndicesBuffer(frameIndex),
    };
    if (auto recorded = cullPipeline.recordDispatch(
            cmd, storageBuffers, &pushData, sizeof(pushData),
            (totalInstanceCount + 63u) / 64u, 1, 1);
        !recorded)
    {
        return recorded;
    }

    // Step 4: COMPUTE_SHADER_WRITE -> DRAW_INDIRECT_READ (instanceCount) and
    // VERTEX_SHADER_READ (compacted indices).
    {
        VkBufferMemoryBarrier2 barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        barriers[0].buffer = indirectBuffer(frameIndex).handle();
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        barriers[1].buffer = visibleIndicesBuffer(frameIndex).handle();
        barriers[1].size = VK_WHOLE_SIZE;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 2;
        dep.pBufferMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
    return {};
}

} // namespace starforge::gpu_resources