#include "vulkan/parallel_secondary.hpp"

#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace starforge::vulkan
{

namespace
{

// Lives on the recorder's stack for the duration of waitAll(); each job owns
// exactly one context, so there is no shared mutable state between jobs.
struct RecordContext
{
    const SecondaryTask* task = nullptr;
    std::uint32_t slot = 0;
    VkCommandBuffer secondary = VK_NULL_HANDLE;
    const SecondaryRecordingConfig* config = nullptr;
    std::thread::id threadId;
    VkResult result = VK_SUCCESS;
};

void recordSecondaryJob(void* userData)
{
    auto* ctx = static_cast<RecordContext*>(userData);
    ctx->threadId = std::this_thread::get_id();
    const SecondaryRecordingConfig& cfg = *ctx->config;

    VkCommandBufferInheritanceRenderingInfoKHR inheritanceRendering{};
    inheritanceRendering.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO_KHR;
    inheritanceRendering.colorAttachmentCount = 1;
    inheritanceRendering.pColorAttachmentFormats = &cfg.colorFormat;
    inheritanceRendering.depthAttachmentFormat = cfg.depthFormat;
    inheritanceRendering.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkCommandBufferInheritanceInfo inheritance{};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.pNext = &inheritanceRendering;
    inheritance.renderPass = VK_NULL_HANDLE; // dynamic rendering
    inheritance.subpass = 0;
    inheritance.framebuffer = VK_NULL_HANDLE;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                  VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    begin.pInheritanceInfo = &inheritance;

    ctx->result = vkBeginCommandBuffer(ctx->secondary, &begin);
    if (ctx->result != VK_SUCCESS) return;

    // Dynamic-rendering viewport convention (Y-down clip space).
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(cfg.height);
    viewport.width = static_cast<float>(cfg.width);
    viewport.height = -static_cast<float>(cfg.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(ctx->secondary, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {cfg.width, cfg.height};
    vkCmdSetScissor(ctx->secondary, 0, 1, &scissor);

    if (ctx->task->fn)
    {
        ctx->task->fn(ctx->secondary, ctx->task->userData, ctx->slot);
    }

    ctx->result = vkEndCommandBuffer(ctx->secondary);
}

} // namespace

std::expected<ParallelSecondaryRecorder, std::string>
ParallelSecondaryRecorder::create(const SecondaryRecordingConfig& config)
{
    if (config.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ParallelSecondaryRecorder requires a device");
    }
    if (config.width == 0 || config.height == 0)
    {
        return std::unexpected("ParallelSecondaryRecorder requires non-zero extent");
    }

    ParallelSecondaryRecorder recorder;
    recorder.config_ = config;
    auto pools = ThreadedCommandPools{};
    if (auto init = pools.init(config.device, config.queueFamily, config.maxThreads, config.framesInFlight);
        !init)
    {
        return std::unexpected(init.error());
    }
    recorder.pools_ = std::move(pools);
    return recorder;
}

std::expected<std::vector<VkCommandBuffer>, std::string>
ParallelSecondaryRecorder::record(std::uint32_t frameIndex,
                                  std::span<const SecondaryTask> tasks,
                                  jobs::JobScheduler& scheduler)
{
    if (tasks.empty()) return std::vector<VkCommandBuffer>{};
    if (tasks.size() > config_.maxThreads)
    {
        return std::unexpected("more recording tasks than thread slots");
    }
    if (frameIndex >= config_.framesInFlight)
    {
        return std::unexpected("frameIndex exceeds framesInFlight");
    }

    std::vector<RecordContext> contexts;
    contexts.reserve(tasks.size());
    std::vector<VkCommandBuffer> secondaries;
    secondaries.reserve(tasks.size());

    // Allocate each secondary from its own pool slot on the calling thread;
    // vkAllocateCommandBuffers on distinct pools is thread-safe, and the jobs
    // only ever use their assigned pool.
    for (std::size_t i = 0; i < tasks.size(); ++i)
    {
        const VkCommandPool pool = pools_.pool(static_cast<std::uint32_t>(i), frameIndex);
        if (pool == VK_NULL_HANDLE)
        {
            return std::unexpected("no command pool for recording slot");
        }

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        alloc.commandBufferCount = 1;

        VkCommandBuffer secondary = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(config_.device, &alloc, &secondary);
            result != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("vkAllocateCommandBuffers (secondary)", result));
        }

        secondaries.push_back(secondary);
        contexts.push_back(RecordContext{&tasks[i], static_cast<std::uint32_t>(i), secondary, &config_});
    }

    std::vector<jobs::TaskHandle> handles;
    handles.reserve(contexts.size());
    for (auto& context : contexts)
    {
        handles.push_back(scheduler.submit(&recordSecondaryJob, &context));
    }
    for (auto handle : handles)
    {
        scheduler.wait(handle);
    }

    lastRecordingThreads_.clear();
    lastRecordingThreads_.reserve(contexts.size());
    for (const auto& context : contexts)
    {
        lastRecordingThreads_.push_back(context.threadId);
        if (context.result != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("secondary command recording", context.result));
        }
    }
    return secondaries;
}

} // namespace starforge::vulkan
