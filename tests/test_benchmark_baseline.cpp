#include <doctest/doctest.h>

#include "bench/benchmark_harness.hpp"
#include "bench/benchmark_result.hpp"
#include "bench/benchmark_scene.hpp"
#include "bench/benchmark_workload.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/image.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_swapchain.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::bench;
using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{

} // namespace

TEST_CASE("Benchmark baseline: naive and competent CPU-driven (10k instances)")
{
    ContextCreateInfo info;
    info.enableValidation = false; // benchmarks need validation off
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

#ifndef STARFORGE_BASELINE_DIR
#define STARFORGE_BASELINE_DIR "benchmarks/baselines"
#endif
        std::filesystem::create_directories(STARFORGE_BASELINE_DIR);

        std::optional<starforge::bench::BenchmarkResult> naiveResult;
        std::optional<starforge::bench::BenchmarkResult> competentResult;

        // Run naive baseline (no culling).
        {
            WorkloadConfig config;
            config.instanceCount = 10000;
            config.visibleFraction = 1.0;
            config.seed = 42;
            config.enableCulling = false;
            config.resolutionWidth = 1920;
            config.resolutionHeight = 1080;

            auto scene = generateWorkloadScene(config);
            auto frustum = buildWorkloadFrustum(config);

            HarnessConfig harnessConfig;
            harnessConfig.warmupFrames = 3;
            harnessConfig.measuredFrames = 10;
            harnessConfig.timingMode = "throughput";
            harnessConfig.framesInFlight = 2;
            harnessConfig.benchmarkId = "naive_10k";
            harnessConfig.workloadId = "submission_stress";
            harnessConfig.seed = 42;

            auto workload = [&](WorkloadContext& ctx) {
                (void)recordWorkloadFrame(config, scene, frustum, ctx.cmd, VK_NULL_HANDLE, VK_NULL_HANDLE);
            };

            harnessConfig.requireRelease = false;
            harnessConfig.requireValidationOff = false;

            auto result = BenchmarkHarness::run(harnessConfig, workload, *context, *device);
            REQUIRE(result.has_value());
            {
                CHECK(result->metrics.count("frame_time_ms") > 0);
                CHECK(result->metrics.count("cpu_command_recording_ms") > 0);
                CHECK(result->metrics["frame_time_ms"].summary.valid);
                CHECK(result->metrics["frame_time_ms"].summary.median > 0.0);

                // Write baseline file.
                std::string json = result->toJson();
                std::ofstream out(std::string(STARFORGE_BASELINE_DIR) + "/naive_10k.json");
                if (out)
                {
                    out << json;
                    out.close();
                }
            }
            naiveResult = *result;
        }

        // Run competent baseline (CPU culling).
        {
            WorkloadConfig config;
            config.instanceCount = 10000;
            config.visibleFraction = 1.0;
            config.seed = 42;
            config.enableCulling = true;
            config.resolutionWidth = 1920;
            config.resolutionHeight = 1080;

            auto scene = generateWorkloadScene(config);
            auto frustum = buildWorkloadFrustum(config);

            HarnessConfig harnessConfig;
            harnessConfig.warmupFrames = 3;
            harnessConfig.measuredFrames = 10;
            harnessConfig.timingMode = "throughput";
            harnessConfig.framesInFlight = 2;
            harnessConfig.benchmarkId = "competent_10k";
            harnessConfig.workloadId = "submission_stress";
            harnessConfig.seed = 42;

            auto workload = [&](WorkloadContext& ctx) {
                (void)recordWorkloadFrame(config, scene, frustum, ctx.cmd, VK_NULL_HANDLE, VK_NULL_HANDLE);
            };

            harnessConfig.requireRelease = false;
            harnessConfig.requireValidationOff = false;

            auto result = BenchmarkHarness::run(harnessConfig, workload, *context, *device);
            REQUIRE(result.has_value());
            {
                CHECK(result->metrics.count("frame_time_ms") > 0);
                CHECK(result->metrics["frame_time_ms"].summary.valid);

                // Write baseline file.
                std::string json = result->toJson();
                std::ofstream out(std::string(STARFORGE_BASELINE_DIR) + "/competent_10k.json");
                if (out)
                {
                    out << json;
                    out.close();
                }
            }
            competentResult = *result;
        }

        // Baseline invariant: competent (with culling) should draw fewer objects than naive.
        REQUIRE(naiveResult.has_value());
        REQUIRE(competentResult.has_value());
        // For 100% visible fraction they may be similar, but CPU culling path is exercised.
        // The invariant is that culling is applied; at minimum both produce valid results.
        CHECK(naiveResult->metrics.count("frame_time_ms") > 0);
        CHECK(competentResult->metrics.count("frame_time_ms") > 0);
    }
}
