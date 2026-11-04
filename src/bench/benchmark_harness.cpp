#include "bench/benchmark_harness.hpp"

#include "bench/cpu_timer.hpp"
#include "core/log.hpp"
#include "gpu_resources/allocator.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace starforge::bench
{

std::expected<void, std::string> BenchmarkHarness::checkPrerequisites(const HarnessConfig& config)
{
    if (config.requireRelease)
    {
#ifdef NDEBUG
        // OK
#else
        return std::unexpected("Benchmarks require a Release build (NDEBUG not defined)");
#endif
    }

    if (config.requireValidationOff)
    {
#ifdef STARFORGE_ENABLE_VALIDATION
        return std::unexpected("Benchmarks require validation disabled (STARFORGE_ENABLE_VALIDATION is defined)");
#endif
    }

    if (config.measuredFrames == 0)
    {
        return std::unexpected("measuredFrames must be > 0");
    }

    if (config.warmupFrames == 0 && config.measuredFrames < 10)
    {
        return std::unexpected("Insufficient measured frames for stable results");
    }

    return {};
}

GpuInfo BenchmarkHarness::collectGpuInfo(const vulkan::VulkanContext& context)
{
    GpuInfo info;
    info.deviceName = context.capabilities().deviceName;
    info.vendorId = context.capabilities().vendorId;
    info.vulkanApiVersion = std::to_string(VK_API_VERSION_MAJOR(context.capabilities().apiVersion)) + "." +
                            std::to_string(VK_API_VERSION_MINOR(context.capabilities().apiVersion)) + "." +
                            std::to_string(VK_API_VERSION_PATCH(context.capabilities().apiVersion));
    // Driver version string is decoded by the capabilities module.
    info.driverVersion = std::to_string(context.capabilities().driverVersion);
    return info;
}

CpuInfo BenchmarkHarness::collectCpuInfo()
{
    CpuInfo info;
    // Read CPU model from /proc/cpuinfo.
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line))
    {
        if (line.find("model name") != std::string::npos)
        {
            auto pos = line.find(':');
            if (pos != std::string::npos)
            {
                info.model = line.substr(pos + 2); // skip ": "
                // Trim trailing whitespace.
                while (!info.model.empty() && (info.model.back() == '\n' || info.model.back() == '\r'))
                {
                    info.model.pop_back();
                }
            }
            break;
        }
    }

    // Count cores/threads.
    std::ifstream cpuinfo2("/proc/cpuinfo");
    std::uint32_t cores = 0;
    while (std::getline(cpuinfo2, line))
    {
        if (line.find("processor") != std::string::npos)
        {
            cores++;
        }
    }
    info.threadCount = cores;
    info.coreCount = cores; // simplified; could parse physical id

    // CPU governor.
    std::ifstream gov("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (gov)
    {
        std::getline(gov, info.governor);
        while (!info.governor.empty() && (info.governor.back() == '\n' || info.governor.back() == '\r'))
        {
            info.governor.pop_back();
        }
    }
    else
    {
        info.governor = "unknown";
    }

    return info;
}

SystemInfo BenchmarkHarness::collectSystemInfo()
{
    SystemInfo info;
    info.os = "Linux";

    // Kernel version.
    std::ifstream version("/proc/version");
    if (version)
    {
        std::getline(version, info.kernel);
    }

    // RAM.
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line))
    {
        if (line.find("MemTotal:") != std::string::npos)
        {
            std::istringstream iss(line);
            std::string label;
            std::uint64_t kb = 0;
            iss >> label >> kb;
            info.ramBytes = kb * 1024;
            break;
        }
    }

    return info;
}

BuildInfo BenchmarkHarness::collectBuildInfo()
{
    BuildInfo info;
    info.compiler = "Clang"; // could be refined
    info.buildType = "Release";
#ifdef NDEBUG
    info.buildType = "Release";
#else
    info.buildType = "Debug";
#endif
#ifdef STARFORGE_ENABLE_VALIDATION
    info.validationEnabled = true;
#else
    info.validationEnabled = false;
#endif
    info.flags = "";
    return info;
}

EnvironmentInfo BenchmarkHarness::collectEnvironmentInfo()
{
    EnvironmentInfo info;
    // GPU clock (amdgpu sysfs).
    std::ifstream gpuClock("/sys/class/drm/card0/device/pp_dpm_sclk");
    if (gpuClock)
    {
        std::string line;
        std::getline(gpuClock, line);
        // Parse the active clock (marked with *).
        // Format: "0: 500Mhz 1: 1000Mhz * 2: 2500Mhz"
        // Just store the raw string for now.
    }

    // GPU temperature.
    std::ifstream gpuTemp("/sys/class/drm/card0/device/hwmon/hwmon0/temp1_input");
    if (gpuTemp)
    {
        int millidegrees = 0;
        gpuTemp >> millidegrees;
        info.gpuTempC = static_cast<double>(millidegrees) / 1000.0;
    }

    // CPU frequency.
    std::ifstream cpuFreq("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (cpuFreq)
    {
        int khz = 0;
        cpuFreq >> khz;
        info.cpuFreqMhz = static_cast<double>(khz) / 1000.0;
    }

    return info;
}

std::expected<BenchmarkResult, std::string>
BenchmarkHarness::run(const HarnessConfig& config,
                     const WorkloadFn& workload,
                     const vulkan::VulkanContext& context,
                     const vulkan::VulkanDevice& device)
{
    // Check prerequisites.
    auto prereq = checkPrerequisites(config);
    if (!prereq)
    {
        return std::unexpected(prereq.error());
    }

    // Latency mode: force single frame in flight.
    HarnessConfig effectiveConfig = config;
    if (effectiveConfig.timingMode == "latency")
    {
        effectiveConfig.framesInFlight = 1;
    }

    // Create timer.
    vulkan::GpuTimer::CreateInfo timerInfo;
    timerInfo.device = device.device();
    timerInfo.physicalDevice = context.physicalDevice();
    timerInfo.queueFamily = device.graphicsQueueFamily();
    timerInfo.framesInFlight = effectiveConfig.framesInFlight;
    timerInfo.maxScopesPerFrame = 8;

    auto timer = vulkan::GpuTimer::create(timerInfo);
    if (!timer)
    {
        return std::unexpected(timer.error());
    }

    // Create command pools (one per frame in flight).
    std::vector<VkCommandPool> cmdPools(effectiveConfig.framesInFlight);
    std::vector<VkCommandBuffer> cmdBuffers(effectiveConfig.framesInFlight);
    std::vector<VkFence> fences(effectiveConfig.framesInFlight);

    for (std::uint32_t i = 0; i < effectiveConfig.framesInFlight; ++i)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (const VkResult result = vkCreateCommandPool(device.device(), &poolInfo, nullptr, &cmdPools[i]);
            result != VK_SUCCESS)
        {
            // Cleanup.
            for (std::uint32_t j = 0; j < i; ++j)
            {
                vkDestroyCommandPool(device.device(), cmdPools[j], nullptr);
            }
            return std::unexpected("Failed to create command pool");
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (const VkResult result = vkAllocateCommandBuffers(device.device(), &allocInfo, &cmdBuffers[i]);
            result != VK_SUCCESS)
        {
            for (std::uint32_t j = 0; j <= i; ++j)
            {
                vkDestroyCommandPool(device.device(), cmdPools[j], nullptr);
            }
            return std::unexpected("Failed to allocate command buffer");
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (const VkResult result = vkCreateFence(device.device(), &fenceInfo, nullptr, &fences[i]);
            result != VK_SUCCESS)
        {
            for (std::uint32_t j = 0; j <= i; ++j)
            {
                vkDestroyCommandPool(device.device(), cmdPools[j], nullptr);
                vkDestroyFence(device.device(), fences[j], nullptr);
            }
            return std::unexpected("Failed to create fence");
        }
    }

    // Warm-up phase.
    for (std::uint32_t i = 0; i < effectiveConfig.warmupFrames; ++i)
    {
        std::uint32_t frameIndex = i % effectiveConfig.framesInFlight;

        vkWaitForFences(device.device(), 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device.device(), 1, &fences[frameIndex]);

        VkCommandBuffer cmd = cmdBuffers[frameIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        timer->resetQueryPool(cmd, frameIndex, timer->maxScopes() * 2);

        WorkloadContext ctx;
        ctx.device = device.device();
        ctx.queue = device.graphicsQueue();
        ctx.cmd = cmd;
        ctx.frameIndex = frameIndex;
        ctx.framesInFlight = effectiveConfig.framesInFlight;
        workload(ctx);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo cmdSubmit{};
        cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit.commandBuffer = cmd;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdSubmit;

        vkQueueSubmit2(device.graphicsQueue(), 1, &submitInfo, fences[frameIndex]);
    }

    // Wait for all warm-up frames to complete.
    vkWaitForFences(device.device(), effectiveConfig.framesInFlight, fences.data(), VK_TRUE, UINT64_MAX);

    // Measured phase.
    FrameTimingCollector collector;
    std::vector<double> gpuDurations;

    auto frameStart = std::chrono::steady_clock::now();

    for (std::uint32_t i = 0; i < effectiveConfig.measuredFrames; ++i)
    {
        std::uint32_t frameIndex = i % effectiveConfig.framesInFlight;

        // Wait for this frame's fence.
        vkWaitForFences(device.device(), 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device.device(), 1, &fences[frameIndex]);

        // Collect GPU results from previous use of this frame slot BEFORE resetting.
        if (timer->collectResults(frameIndex))
        {
            double gpuDurationNs = timer->scopeDurationNs(frameIndex, 0);
            if (gpuDurationNs >= 0.0)
            {
                gpuDurations.push_back(gpuDurationNs / 1'000'000.0); // ns to ms
            }
        }

        VkCommandBuffer cmd = cmdBuffers[frameIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        timer->resetQueryPool(cmd, frameIndex, timer->maxScopes() * 2);

        // CPU timing.
        CpuTimer cpuTimer;
        cpuTimer.beginFrame();
        cpuTimer.beginCommandRecording();

        WorkloadContext ctx;
        ctx.device = device.device();
        ctx.queue = device.graphicsQueue();
        ctx.cmd = cmd;
        ctx.frameIndex = frameIndex;
        ctx.framesInFlight = effectiveConfig.framesInFlight;

        // Begin a named scope for the workload.
        timer->beginScope(cmd, frameIndex, "workload");

        workload(ctx);

        timer->endScope(cmd, frameIndex);

        cpuTimer.endCommandRecording();
        cpuTimer.beginSubmission();

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo cmdSubmit{};
        cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit.commandBuffer = cmd;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdSubmit;

        vkQueueSubmit2(device.graphicsQueue(), 1, &submitInfo, fences[frameIndex]);

        cpuTimer.endSubmission();
        cpuTimer.endFrame();

        // Record sample.
        FrameTimingSample sample;
        sample.framePreparationMs = cpuTimer.framePreparationMs();
        sample.commandRecordingMs = cpuTimer.commandRecordingMs();
        sample.submissionMs = cpuTimer.submissionMs();
        sample.totalFrameMs = cpuTimer.totalFrameMs();
        collector.addSample(sample);
    }

    // Wait for all measured frames to complete.
    vkWaitForFences(device.device(), effectiveConfig.framesInFlight, fences.data(), VK_TRUE, UINT64_MAX);

    // Collect any remaining GPU results.
    for (std::uint32_t i = 0; i < effectiveConfig.framesInFlight; ++i)
    {
        timer->collectResults(i);
        double gpuDuration = timer->scopeDurationNs(i, 0);
        if (gpuDuration >= 0.0)
        {
            gpuDurations.push_back(gpuDuration);
        }
    }

    auto frameEnd = std::chrono::steady_clock::now();
    (void)frameStart;
    (void)frameEnd;

    // Build result.
    BenchmarkResult result;
    result.benchmarkId = effectiveConfig.benchmarkId;
    result.workloadId = effectiveConfig.workloadId;
    result.timingMode = effectiveConfig.timingMode;
    result.source.commitHash = "unknown"; // TODO: git rev-parse
    result.source.dirty = false;

    result.gpu = collectGpuInfo(context);
    result.cpu = collectCpuInfo();
    result.system = collectSystemInfo();
    result.build = collectBuildInfo();
    result.environment = collectEnvironmentInfo();

    // Metrics.
    auto frameStats = collector.totalFrameStats();
    Metric frameMetric;
    frameMetric.summary = frameStats;
    frameMetric.rawSamples = collector.totalFrameSamples();
    result.metrics["frame_time_ms"] = frameMetric;

    auto cmdStats = collector.commandRecordingStats();
    Metric cmdMetric;
    cmdMetric.summary = cmdStats;
    cmdMetric.rawSamples = collector.commandRecordingSamples();
    result.metrics["cpu_command_recording_ms"] = cmdMetric;

    auto subStats = collector.submissionStats();
    Metric subMetric;
    subMetric.summary = subStats;
    subMetric.rawSamples = collector.submissionSamples();
    result.metrics["cpu_submission_ms"] = subMetric;

    if (!gpuDurations.empty())
    {
        // Compute GPU stats from collected durations.
        stats::Statistics gpuStats = stats::compute(gpuDurations);
        Metric gpuMetric;
        gpuMetric.summary = gpuStats;
        gpuMetric.rawSamples = gpuDurations;
        result.metrics["gpu_time_ms"] = gpuMetric;
    }

    // Memory snapshot (after benchmark).
    // (MemorySnapshot capture requires allocator; not available here.
    //  Caller should capture separately.)

    // Cleanup.
    for (std::uint32_t i = 0; i < effectiveConfig.framesInFlight; ++i)
    {
        vkDestroyFence(device.device(), fences[i], nullptr);
        vkDestroyCommandPool(device.device(), cmdPools[i], nullptr);
    }

    return result;
}

} // namespace starforge::bench
