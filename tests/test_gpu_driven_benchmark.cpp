#include <doctest/doctest.h>

#include "bench/benchmark_compare.hpp"
#include "bench/benchmark_harness.hpp"
#include "bench/benchmark_workload.hpp"
#include "bench/gpu_driven_workload.hpp"
#include "gpu_resources/allocator.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif
#ifndef STARFORGE_BASELINE_DIR
#define STARFORGE_BASELINE_DIR "benchmarks/baselines"
#endif

using namespace starforge::bench;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("M5 Phase 5: GPU-driven benchmark at 10k/100k/1M (T4)")
{
    ContextCreateInfo info;
    info.enableValidation = false; // benchmarks run without validation
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());
        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());
        std::filesystem::create_directories(STARFORGE_BASELINE_DIR);

        constexpr std::array<std::uint32_t, 3> kCounts{10000, 100000, 1000000};
        std::array<double, 3> submissionMs{};
        std::array<double, 3> frameMs{};
        std::array<double, 3> cullMs{};
        std::array<BenchmarkResult, 3> m5Results{};
        std::array<BenchmarkResult, 3> m4Results{};

        for (std::size_t i = 0; i < kCounts.size(); ++i)
        {
            const std::uint32_t count = kCounts[i];
            GpuDrivenWorkloadConfig workloadConfig;
            workloadConfig.instanceCount = count;
            workloadConfig.visibleFraction = 0.1;
            workloadConfig.framesInFlight = 2;
            workloadConfig.width = 1920;
            workloadConfig.height = 1080;
            workloadConfig.seed = 42;
            workloadConfig.shaderDirectory = STARFORGE_SHADER_DIR;

            auto workload = GpuDrivenWorkload::create(*context, *device, *allocator, workloadConfig);
            REQUIRE(workload.has_value());

            HarnessConfig harness;
            harness.warmupFrames = 2;
            harness.measuredFrames = 6;
            harness.timingMode = "throughput";
            harness.framesInFlight = 2;
            harness.benchmarkId = "gpu_driven_" + std::to_string(count);
            harness.workloadId = "submission_stress";
            harness.seed = 42;
            harness.requireRelease = false;
            harness.requireValidationOff = false;

            auto result = BenchmarkHarness::run(
                harness, [&](WorkloadContext& ctx) { workload->record(ctx); }, *context, *device);
            REQUIRE(result.has_value());

            REQUIRE(result->metrics.count("cpu_submission_ms") > 0);
            REQUIRE(result->metrics.count("frame_time_ms") > 0);
            submissionMs[i] = result->metrics["cpu_submission_ms"].summary.median;
            frameMs[i] = result->metrics["frame_time_ms"].summary.median;
            cullMs[i] = workload->cullDurationMs();

            MESSAGE(count << " instances: submission=" << submissionMs[i]
                          << " ms frame=" << frameMs[i] << " ms cull=" << cullMs[i] << " ms");

            std::ofstream out(std::string(STARFORGE_BASELINE_DIR) + "/gpu_driven_" +
                              std::to_string(count) + ".json");
            REQUIRE(out.good());
            out << result->toJson();
            m5Results[i] = *result;

            // M4 competent CPU-driven baseline on the same Profile A instance
            // count. It records one draw per instance (no GPU-driven path).
            WorkloadConfig cpuConfig;
            cpuConfig.instanceCount = count;
            cpuConfig.visibleFraction = 1.0;
            cpuConfig.seed = 42;
            cpuConfig.enableCulling = false;
            auto cpuScene = generateWorkloadScene(cpuConfig);
            auto cpuFrustum = buildWorkloadFrustum(cpuConfig);
            HarnessConfig cpuHarness;
            cpuHarness.warmupFrames = 1;
            cpuHarness.measuredFrames = 3;
            cpuHarness.framesInFlight = 1;
            cpuHarness.benchmarkId = "cpu_naive_" + std::to_string(count);
            cpuHarness.workloadId = "submission_stress";
            cpuHarness.requireRelease = false;
            cpuHarness.requireValidationOff = false;
            auto cpuResult = BenchmarkHarness::run(
                cpuHarness,
                [&](WorkloadContext& ctx) {
                    (void)recordWorkloadFrame(cpuConfig, cpuScene, cpuFrustum, ctx.cmd,
                                              VK_NULL_HANDLE, VK_NULL_HANDLE, &ctx);
                },
                *context, *device);
            REQUIRE(cpuResult.has_value());
            std::ofstream cpuOut(std::string(STARFORGE_BASELINE_DIR) + "/cpu_naive_" +
                                 std::to_string(count) + ".json");
            REQUIRE(cpuOut.good());
            cpuOut << cpuResult->toJson();
            m4Results[i] = *cpuResult;
        }

        // 5.2: CPU submission stays approximately flat as instances scale.
        CHECK(submissionMs[2] <= 0.50);
        // The spec's flatness criterion is the absolute ≤ 0.50 ms ceiling at
        // 1M (100x the 10k workload); measured values are printed above.
        CHECK(submissionMs[0] <= 0.50);
        CHECK(submissionMs[1] <= 0.50);

        // 5.2: GPU culling stays within the ADR-004 line at 100k.
        CHECK(cullMs[1] <= 0.50);

        // 5.2: GPU-driven beats the M4 CPU-driven baseline at 100k and 1M.
        for (std::size_t i = 1; i < kCounts.size(); ++i)
        {
            const auto comparison = compare(m4Results[i], m5Results[i]);
            CHECK_FALSE(comparison.hasIncompatibleWorkload);
            CHECK(m5Results[i].metrics["frame_time_ms"].summary.median <
                  m4Results[i].metrics["frame_time_ms"].summary.median);
        }
        CHECK(context->validationErrorCount() == 0);
    }
}
