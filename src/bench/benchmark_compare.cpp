#include "bench/benchmark_compare.hpp"

#include <cmath>
#include <string>

namespace starforge::bench
{

std::expected<void, std::string> checkCompatibility(const BenchmarkResult& baseline, const BenchmarkResult& candidate)
{
    if (baseline.workloadId != candidate.workloadId)
    {
        return std::unexpected("Workload IDs differ: " + baseline.workloadId + " vs " + candidate.workloadId);
    }
    if (baseline.timingMode != candidate.timingMode)
    {
        return std::unexpected("Timing modes differ: " + baseline.timingMode + " vs " + candidate.timingMode);
    }
    if (baseline.workload.sceneId != candidate.workload.sceneId)
    {
        return std::unexpected("Scene IDs differ");
    }
    if (baseline.workload.meshComplexity != candidate.workload.meshComplexity)
    {
        return std::unexpected("Mesh complexities differ");
    }
    if (baseline.workload.instanceCount != candidate.workload.instanceCount)
    {
        return std::unexpected("Instance counts differ");
    }
    if (std::fabs(baseline.workload.visibleFraction - candidate.workload.visibleFraction) > 1e-6)
    {
        return std::unexpected("Visible fractions differ");
    }
    if (baseline.workload.resolutionWidth != candidate.workload.resolutionWidth ||
        baseline.workload.resolutionHeight != candidate.workload.resolutionHeight)
    {
        return std::unexpected("Resolutions differ");
    }
    return {};
}

CompareResult compare(const BenchmarkResult& baseline, const BenchmarkResult& candidate)
{
    CompareResult result;

    // Check workload compatibility.
    auto compat = checkCompatibility(baseline, candidate);
    if (!compat)
    {
        result.hasIncompatibleWorkload = true;
        result.incompatibilityReason = compat.error();
        return result;
    }

    // Check GPU match.
    if (baseline.gpu.deviceName != candidate.gpu.deviceName ||
        baseline.gpu.driverVersion != candidate.gpu.driverVersion)
    {
        result.hasGpuMismatch = true;
        result.gpuMismatchReason = "GPU or driver differs: " + baseline.gpu.deviceName + " (" + baseline.gpu.driverVersion +
                                  ") vs " + candidate.gpu.deviceName + " (" + candidate.gpu.driverVersion + ")";
    }

    // Check build match.
    if (baseline.build.compiler != candidate.build.compiler ||
        baseline.build.buildType != candidate.build.buildType)
    {
        result.hasBuildMismatch = true;
        result.buildMismatchReason = "Build config differs: " + baseline.build.compiler + " " + baseline.build.buildType +
                                    " vs " + candidate.build.compiler + " " + candidate.build.buildType;
    }

    // Compare metrics.
    for (const auto& [name, baseMetric] : baseline.metrics)
    {
        auto candIt = candidate.metrics.find(name);
        if (candIt == candidate.metrics.end())
        {
            continue; // skip missing metrics
        }

        const auto& candMetric = candIt->second;

        // Skip unmeasurable metrics.
        if (!baseMetric.summary.valid || !candMetric.summary.valid)
        {
            continue;
        }

        CompareResult::MetricComparison comp;
        comp.metricName = name;
        comp.baselineValue = baseMetric.summary.median;
        comp.candidateValue = candMetric.summary.median;

        if (baseMetric.summary.median == 0.0)
        {
            comp.ratio = (candMetric.summary.median == 0.0) ? 1.0 : std::numeric_limits<double>::infinity();
            comp.percentChange = (candMetric.summary.median == 0.0) ? 0.0 : 100.0;
            if (candMetric.summary.median > 0.0)
            {
                comp.isRegression = true;
                result.hasRegression = true;
            }
        }
        else
        {
            comp.ratio = candMetric.summary.median / baseMetric.summary.median;
            comp.percentChange = (candMetric.summary.median - baseMetric.summary.median) / baseMetric.summary.median * 100.0;

            // Determine if this is a time metric (lower is better) or throughput metric (higher is better).
            bool isTimeMetric = (name.find("_ms") != std::string::npos) ||
                                (name.find("_time") != std::string::npos) ||
                                (name.find("_duration") != std::string::npos);

            if (isTimeMetric)
            {
                if (comp.percentChange > result.noiseThresholdPercent)
                {
                    comp.isRegression = true;
                    result.hasRegression = true;
                }
                else if (comp.percentChange < -result.noiseThresholdPercent)
                {
                    comp.isImprovement = true;
                }
            }
            else
            {
                // Throughput metric: higher is better, so decrease is regression.
                if (comp.percentChange < -result.noiseThresholdPercent)
                {
                    comp.isRegression = true;
                    result.hasRegression = true;
                }
                else if (comp.percentChange > result.noiseThresholdPercent)
                {
                    comp.isImprovement = true;
                }
            }
        }

        result.comparisons.push_back(comp);
    }

    return result;
}

} // namespace starforge::bench
