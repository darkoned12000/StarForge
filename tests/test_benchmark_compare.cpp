#include <doctest/doctest.h>

#include "bench/benchmark_compare.hpp"
#include "bench/benchmark_result.hpp"

using namespace starforge::bench;

namespace
{

BenchmarkResult makeResult(const std::string& id, const std::string& workload,
                           const std::string& gpu, const std::string& timingMode,
                           double gpuMedian, double cpuMedian)
{
    BenchmarkResult r;
    r.benchmarkId = id;
    r.workloadId = workload;
    r.timingMode = timingMode;
    r.gpu.deviceName = gpu;

    Metric gpuM;
    gpuM.summary.median = gpuMedian;
    gpuM.summary.valid = true;
    r.metrics["gpu_time_ms"] = gpuM;

    Metric cpuM;
    cpuM.summary.median = cpuMedian;
    cpuM.summary.valid = true;
    r.metrics["cpu_time_ms"] = cpuM;

    return r;
}

} // namespace

TEST_CASE("Compare: identical results show no regression")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);

    auto result = compare(base, cand);
    CHECK_FALSE(result.hasIncompatibleWorkload);
    CHECK_FALSE(result.hasGpuMismatch);
    CHECK_FALSE(result.hasRegression);
    CHECK(result.comparisons.size() == 2);

    for (const auto& c : result.comparisons)
    {
        CHECK(c.percentChange == doctest::Approx(0.0));
        CHECK_FALSE(c.isRegression);
        CHECK_FALSE(c.isImprovement);
    }
}

TEST_CASE("Compare: candidate slower by > threshold is regression")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 6.0, 1.0); // 20% slower

    auto result = compare(base, cand);
    CHECK_FALSE(result.hasIncompatibleWorkload);
    CHECK(result.hasRegression);
    CHECK(result.comparisons[1].isRegression); // gpu_time_ms (alphabetically second)
}

TEST_CASE("Compare: candidate faster by > threshold is improvement")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 4.0, 1.0); // 20% faster

    auto result = compare(base, cand);
    CHECK_FALSE(result.hasRegression);
    CHECK(result.comparisons[1].isImprovement);
}

TEST_CASE("Compare: small difference within noise is neither")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.1, 1.0); // 2% slower, within 5% noise

    auto result = compare(base, cand);
    CHECK_FALSE(result.hasRegression);
    CHECK_FALSE(result.comparisons[0].isRegression);
    CHECK_FALSE(result.comparisons[0].isImprovement);
}

TEST_CASE("Compare: incompatible workloads detected")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload2", "RX 7900 XTX", "throughput", 5.0, 1.0);

    auto result = compare(base, cand);
    CHECK(result.hasIncompatibleWorkload);
    CHECK_FALSE(result.incompatibilityReason.empty());
}

TEST_CASE("Compare: timing mode mismatch detected")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "latency", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);

    auto result = compare(base, cand);
    CHECK(result.hasIncompatibleWorkload);
}

TEST_CASE("Compare: GPU mismatch detected")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RTX 4090", "throughput", 5.0, 1.0);

    auto result = compare(base, cand);
    CHECK(result.hasGpuMismatch);
    CHECK_FALSE(result.gpuMismatchReason.empty());
}

TEST_CASE("Compare: build mismatch detected")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    base.build.compiler = "Clang 23.1.4";
    base.build.buildType = "Release";

    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    cand.build.compiler = "GCC 14.2";
    cand.build.buildType = "Release";

    auto result = compare(base, cand);
    CHECK(result.hasBuildMismatch);
}

TEST_CASE("Compare: ratio and percent change calculated correctly")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 10.0, 2.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 15.0, 1.0);

    auto result = compare(base, cand);
    CHECK(result.comparisons.size() == 2);

    // cpu_time_ms comes first alphabetically: 1/2 = 0.5, -50%
    CHECK(result.comparisons[0].ratio == doctest::Approx(0.5));
    CHECK(result.comparisons[0].percentChange == doctest::Approx(-50.0));

    // gpu_time_ms: 15/10 = 1.5, +50%
    CHECK(result.comparisons[1].ratio == doctest::Approx(1.5));
    CHECK(result.comparisons[1].percentChange == doctest::Approx(50.0));
}

TEST_CASE("Compare: missing metric in candidate is skipped")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    cand.metrics.erase("cpu_time_ms");

    auto result = compare(base, cand);
    CHECK(result.comparisons.size() == 1); // only gpu_time_ms
}

TEST_CASE("Compare: checkCompatibility rejects different instance counts")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    base.workload.instanceCount = 100000;

    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    cand.workload.instanceCount = 200000;

    auto compat = checkCompatibility(base, cand);
    CHECK_FALSE(compat.has_value());
}

TEST_CASE("Compare: checkCompatibility accepts identical configs")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);

    auto compat = checkCompatibility(base, cand);
    CHECK(compat.has_value());
}

TEST_CASE("Compare: zero baseline median avoids division by zero")
{
    auto base = makeResult("base", "workload1", "RX 7900 XTX", "throughput", 0.0, 1.0);
    auto cand = makeResult("cand", "workload1", "RX 7900 XTX", "throughput", 5.0, 1.0);

    auto result = compare(base, cand);
    // Should not crash; ratio defaults to 1.0 when baseline is 0.
    CHECK(result.comparisons[0].ratio == doctest::Approx(1.0));
}
