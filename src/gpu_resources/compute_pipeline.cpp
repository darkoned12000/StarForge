#include "gpu_resources/compute_pipeline.hpp"

#include "vulkan/vulkan_result.hpp"

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace starforge::gpu_resources
{

std::expected<ComputePipeline, std::string>
ComputePipeline::createFromFile(const starforge::vulkan::VulkanDevice& device, const std::string& path,
                                const char* entryPoint, std::uint32_t storageBufferCount,
                                std::uint32_t pushConstantSize)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file)
    {
        return std::unexpected("Failed to open SPIR-V file: " + path);
    }

    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || (size % sizeof(std::uint32_t)) != 0)
    {
        return std::unexpected("Invalid SPIR-V file size: " + path);
    }

    std::vector<std::uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

    return create(device, CreateInfo{
        .spirv = bytes,
        .entryPoint = entryPoint,
        .storageBufferCount = storageBufferCount,
        .pushConstantSize = pushConstantSize,
    });
}

std::expected<ComputePipeline, std::string>
ComputePipeline::create(const starforge::vulkan::VulkanDevice& device, const CreateInfo& info)
{
    if (device.device() == VK_NULL_HANDLE || device.graphicsQueue() == VK_NULL_HANDLE)
    {
        return std::unexpected("ComputePipeline requires a valid VulkanDevice");
    }
    if (info.spirv.empty() || (info.spirv.size() % sizeof(std::uint32_t)) != 0)
    {
        return std::unexpected("SPIR-V byte buffer must be non-empty and a multiple of 4 bytes");
    }
    if (info.storageBufferCount == 0)
    {
        return std::unexpected("storageBufferCount must be > 0");
    }

    ComputePipeline cp;
    cp.owner_ = &device;
    cp.device_ = device.device();
    cp.queueFamily_ = device.graphicsQueueFamily();
    cp.storageBufferCount_ = info.storageBufferCount;

    std::vector<std::uint32_t> alignedCode(info.spirv.size() / sizeof(std::uint32_t));
    std::memcpy(alignedCode.data(), info.spirv.data(), info.spirv.size());

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = info.spirv.size();
    shaderInfo.pCode = alignedCode.data();

    if (const VkResult result = vkCreateShaderModule(cp.device_, &shaderInfo, nullptr, &cp.shaderModule_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateShaderModule", result));
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings(info.storageBufferCount);
    for (std::uint32_t i = 0; i < info.storageBufferCount; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (const VkResult result = vkCreateDescriptorSetLayout(cp.device_, &layoutInfo, nullptr, &cp.descriptorSetLayout_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateDescriptorSetLayout", result));
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = info.pushConstantSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &cp.descriptorSetLayout_;
    if (info.pushConstantSize > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    }

    if (const VkResult result = vkCreatePipelineLayout(cp.device_, &pipelineLayoutInfo, nullptr, &cp.pipelineLayout_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreatePipelineLayout", result));
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = info.storageBufferCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (const VkResult result = vkCreateDescriptorPool(cp.device_, &poolInfo, nullptr, &cp.descriptorPool_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateDescriptorPool", result));
    }

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = cp.descriptorPool_;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &cp.descriptorSetLayout_;

    if (const VkResult result = vkAllocateDescriptorSets(cp.device_, &allocSetInfo, &cp.descriptorSet_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkAllocateDescriptorSets", result));
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = cp.shaderModule_;
    pipelineInfo.stage.pName = info.entryPoint ? info.entryPoint : "main";
    pipelineInfo.layout = cp.pipelineLayout_;

    if (const VkResult result = vkCreateComputePipelines(cp.device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cp.pipeline_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateComputePipelines", result));
    }

    // Shader module is no longer needed after pipeline creation.
    vkDestroyShaderModule(cp.device_, cp.shaderModule_, nullptr);
    cp.shaderModule_ = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = cp.queueFamily_;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (const VkResult result = vkCreateCommandPool(cp.device_, &cmdPoolInfo, nullptr, &cp.commandPool_);
        result != VK_SUCCESS)
    {
        cp.destroy();
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateCommandPool", result));
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = cp.commandPool_;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    if (const VkResult result = vkAllocateCommandBuffers(cp.device_, &cmdAlloc, &cp.commandBuffer_);
        result != VK_SUCCESS)
    {
        cp.destroy();
        return std::unexpected(starforge::vulkan::describeFailure("vkAllocateCommandBuffers", result));
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (const VkResult result = vkCreateFence(cp.device_, &fenceInfo, nullptr, &cp.fence_);
        result != VK_SUCCESS)
    {
        cp.destroy();
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateFence", result));
    }

    return cp;
}

ComputePipeline::~ComputePipeline()
{
    destroy();
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queueFamily_(other.queueFamily_),
      storageBufferCount_(other.storageBufferCount_),
      shaderModule_(std::exchange(other.shaderModule_, VK_NULL_HANDLE)),
      descriptorSetLayout_(std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE)),
      descriptorSet_(std::exchange(other.descriptorSet_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE)),
      commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
      commandBuffer_(std::exchange(other.commandBuffer_, VK_NULL_HANDLE)),
      fence_(std::exchange(other.fence_, VK_NULL_HANDLE)),
      operationCount_(std::exchange(other.operationCount_, 0)),
      descriptorUpdateCount_(std::exchange(other.descriptorUpdateCount_, 0)),
      boundBuffers_(std::move(other.boundBuffers_))
{
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        owner_ = std::exchange(other.owner_, nullptr);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        storageBufferCount_ = other.storageBufferCount_;
        shaderModule_ = std::exchange(other.shaderModule_, VK_NULL_HANDLE);
        descriptorSetLayout_ = std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
        descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
        commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
        fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
        operationCount_ = std::exchange(other.operationCount_, 0);
        descriptorUpdateCount_ = std::exchange(other.descriptorUpdateCount_, 0);
        boundBuffers_ = std::move(other.boundBuffers_);
    }
    return *this;
}

void ComputePipeline::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
        // Destroying the pool implicitly frees its command buffers.
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (descriptorSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        if (shaderModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, shaderModule_, nullptr);
    }
    owner_ = nullptr;
    device_ = VK_NULL_HANDLE;
    queueFamily_ = 0;
    storageBufferCount_ = 0;
    shaderModule_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    operationCount_ = 0;
    descriptorUpdateCount_ = 0;
    boundBuffers_.clear();
}

std::expected<void, std::string>
ComputePipeline::bind(std::span<const Buffer* const> storageBuffers)
{
    if (storageBuffers.size() != storageBufferCount_)
    {
        return std::unexpected("storageBuffers count does not match pipeline descriptor layout");
    }

    std::vector<VkBuffer> handles;
    handles.reserve(storageBuffers.size());
    for (const Buffer* buffer : storageBuffers)
    {
        if (!buffer || buffer->handle() == VK_NULL_HANDLE)
        {
            return std::unexpected("Null storage buffer passed to bind");
        }
        handles.push_back(buffer->handle());
    }

    std::lock_guard lock(descriptorMutex_);
    if (handles == boundBuffers_) return {}; // already bound — no update needed

    std::vector<VkDescriptorBufferInfo> infos(storageBuffers.size());
    std::vector<VkWriteDescriptorSet> writes(storageBuffers.size());
    for (std::size_t i = 0; i < storageBuffers.size(); ++i)
    {
        infos[i].buffer = handles[i];
        infos[i].offset = 0;
        infos[i].range = storageBuffers[i]->size();

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet_;
        writes[i].dstBinding = static_cast<std::uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    boundBuffers_ = std::move(handles);
    ++descriptorUpdateCount_;
    return {};
}

std::expected<void, std::string>
ComputePipeline::dispatchAndWait(std::span<const Buffer* const> storageBuffers,
                                 std::uint32_t groupCountX,
                                 std::uint32_t groupCountY,
                                 std::uint32_t groupCountZ)
{
    if (device_ == VK_NULL_HANDLE || pipeline_ == VK_NULL_HANDLE)
    {
        return std::unexpected("ComputePipeline not initialized");
    }
    if (storageBuffers.size() != storageBufferCount_)
    {
        return std::unexpected("storageBuffers count does not match pipeline descriptor layout");
    }
    if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
    {
        return std::unexpected("dispatch dimensions must be > 0 on all axes");
    }

    // Descriptor setup is cached: repeated dispatches reuse the bound set.
    if (auto bound = bind(storageBuffers); !bound) return bound;

    std::vector<VkBufferMemoryBarrier2> preBarriers(storageBuffers.size());
    std::vector<VkBufferMemoryBarrier2> postBarriers(storageBuffers.size());

    for (std::size_t i = 0; i < storageBuffers.size(); ++i)
    {
        if (!storageBuffers[i] || storageBuffers[i]->handle() == VK_NULL_HANDLE)
        {
            return std::unexpected("Null storage buffer passed to dispatchAndWait");
        }

        preBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        preBarriers[i].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        preBarriers[i].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        preBarriers[i].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        preBarriers[i].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        preBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarriers[i].buffer = storageBuffers[i]->handle();
        preBarriers[i].offset = 0;
        preBarriers[i].size = VK_WHOLE_SIZE;

        postBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        postBarriers[i].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        postBarriers[i].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        postBarriers[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
        postBarriers[i].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_HOST_READ_BIT;
        postBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarriers[i].buffer = storageBuffers[i]->handle();
        postBarriers[i].offset = 0;
        postBarriers[i].size = VK_WHOLE_SIZE;
    }

    std::lock_guard lock(mutex_);
    if (const VkResult result = vkResetFences(device_, 1, &fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetFences", result));
    }
    if (const VkResult result = vkResetCommandBuffer(commandBuffer_, 0); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetCommandBuffer", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkBeginCommandBuffer", result));
    }

    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.bufferMemoryBarrierCount = static_cast<std::uint32_t>(preBarriers.size());
    preDep.pBufferMemoryBarriers = preBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &preDep);

    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    vkCmdDispatch(commandBuffer_, groupCountX, groupCountY, groupCountZ);

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.bufferMemoryBarrierCount = static_cast<std::uint32_t>(postBarriers.size());
    postDep.pBufferMemoryBarriers = postBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &postDep);

    if (const VkResult result = vkEndCommandBuffer(commandBuffer_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkEndCommandBuffer", result));
    }

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = commandBuffer_;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;

    if (const VkResult result = owner_->submit(1, &submitInfo, fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkQueueSubmit2", result));
    }
    if (const VkResult result = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences", result));
    }

    ++operationCount_;
    return {};
}

std::expected<void, std::string>
ComputePipeline::dispatchAndWaitWithPushConstants(std::span<const Buffer* const> storageBuffers,
                                                 const void* pushData,
                                                 std::uint32_t pushSize,
                                                 std::uint32_t groupCountX,
                                                 std::uint32_t groupCountY,
                                                 std::uint32_t groupCountZ)
{
    if (device_ == VK_NULL_HANDLE || pipeline_ == VK_NULL_HANDLE)
    {
        return std::unexpected("ComputePipeline not initialized");
    }
    if (storageBuffers.size() != storageBufferCount_)
    {
        return std::unexpected("storageBuffers count does not match pipeline descriptor layout");
    }
    if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
    {
        return std::unexpected("dispatch dimensions must be > 0 on all axes");
    }

    // Descriptor setup is cached: repeated dispatches reuse the bound set.
    if (auto bound = bind(storageBuffers); !bound) return bound;

    std::vector<VkBufferMemoryBarrier2> preBarriers(storageBuffers.size());
    std::vector<VkBufferMemoryBarrier2> postBarriers(storageBuffers.size());

    for (std::size_t i = 0; i < storageBuffers.size(); ++i)
    {
        if (!storageBuffers[i] || storageBuffers[i]->handle() == VK_NULL_HANDLE)
        {
            return std::unexpected("Null storage buffer passed to dispatchAndWait");
        }

        preBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        preBarriers[i].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        preBarriers[i].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        preBarriers[i].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        preBarriers[i].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        preBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarriers[i].buffer = storageBuffers[i]->handle();
        preBarriers[i].offset = 0;
        preBarriers[i].size = VK_WHOLE_SIZE;

        postBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        postBarriers[i].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        postBarriers[i].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        postBarriers[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
        postBarriers[i].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_HOST_READ_BIT;
        postBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarriers[i].buffer = storageBuffers[i]->handle();
        postBarriers[i].offset = 0;
        postBarriers[i].size = VK_WHOLE_SIZE;
    }

    std::lock_guard lock(mutex_);
    if (const VkResult result = vkResetFences(device_, 1, &fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetFences", result));
    }
    if (const VkResult result = vkResetCommandBuffer(commandBuffer_, 0); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetCommandBuffer", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkBeginCommandBuffer", result));
    }

    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.bufferMemoryBarrierCount = static_cast<std::uint32_t>(preBarriers.size());
    preDep.pBufferMemoryBarriers = preBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &preDep);

    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    if (pushData && pushSize > 0)
    {
        vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
    }

    vkCmdDispatch(commandBuffer_, groupCountX, groupCountY, groupCountZ);

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.bufferMemoryBarrierCount = static_cast<std::uint32_t>(postBarriers.size());
    postDep.pBufferMemoryBarriers = postBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &postDep);

    if (const VkResult result = vkEndCommandBuffer(commandBuffer_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkEndCommandBuffer", result));
    }

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = commandBuffer_;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;

    if (const VkResult result = owner_->submit(1, &submitInfo, fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkQueueSubmit2", result));
    }
    if (const VkResult result = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences", result));
    }

    ++operationCount_;
    return {};
}

// placeholder

std::expected<void, std::string>
ComputePipeline::recordDispatch(VkCommandBuffer cmd,
                               std::span<const Buffer* const> storageBuffers,
                               const void* pushData,
                               std::uint32_t pushSize,
                               std::uint32_t groupCountX,
                               std::uint32_t groupCountY,
                               std::uint32_t groupCountZ)
{
    if (device_ == VK_NULL_HANDLE || pipeline_ == VK_NULL_HANDLE)
    {
        return std::unexpected("ComputePipeline not initialized");
    }
    if (storageBuffers.size() != storageBufferCount_)
    {
        return std::unexpected("storageBuffers count does not match pipeline descriptor layout");
    }
    if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
    {
        return std::unexpected("dispatch dimensions must be > 0 on all axes");
    }

    if (auto bound = bind(storageBuffers); !bound) return bound;

    // The descriptor set is shared by all records; bind() serialized its update.
    std::lock_guard lock(mutex_);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);

    if (pushSize > 0 && pushData != nullptr)
    {
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
    }

    vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);

    return {};
}
} // namespace starforge::gpu_resources
