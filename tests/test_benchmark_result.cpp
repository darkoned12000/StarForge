#include <doctest/doctest.h>

#include "bench/benchmark_result.hpp"

#include <string>

using namespace starforge::bench;

TEST_CASE("BenchmarkResult: default construction")
{
    BenchmarkResult result;
    CHECK(result.schemaVersion == kSchemaVersion);
    CHECK(result.benchmarkId.empty());
    CHECK(result.metrics.empty());
}

TEST_CASE("BenchmarkResult: JSON round-trip (empty)")
{
    BenchmarkResult result;
    result.benchmarkId = "test_bench";
    result.workloadId = "test_workload";
    result.timingMode = "latency";

    std::string json = result.toJson();
    CHECK(!json.empty());
    CHECK(json.find("\"schema_version\"") != std::string::npos);
    CHECK(json.find("\"benchmark_id\"") != std::string::npos);

    auto parsed = BenchmarkResult::fromJson(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->schemaVersion == result.schemaVersion);
    CHECK(parsed->benchmarkId == "test_bench");
    CHECK(parsed->workloadId == "test_workload");
    CHECK(parsed->timingMode == "latency");
}

TEST_CASE("BenchmarkResult: JSON round-trip (with metrics)")
{
    BenchmarkResult result;
    result.benchmarkId = "gpu_bench";

    Metric m;
    m.summary.min = 1.0;
    m.summary.max = 5.0;
    m.summary.mean = 3.0;
    m.summary.median = 3.0;
    m.summary.p95 = 4.5;
    m.summary.p99 = 4.9;
    m.summary.stddev = 1.2;
    m.summary.count = 100;
    m.summary.valid = true;
    m.rawSamples = {1.0, 2.0, 3.0, 4.0, 5.0};
    result.metrics["gpu_time_ms"] = m;

    std::string json = result.toJson();
    auto parsed = BenchmarkResult::fromJson(json);
    REQUIRE(parsed.has_value());

    CHECK(parsed->metrics.count("gpu_time_ms") == 1);
    const auto& pm = parsed->metrics["gpu_time_ms"];
    CHECK(pm.summary.min == doctest::Approx(1.0));
    CHECK(pm.summary.max == doctest::Approx(5.0));
    CHECK(pm.summary.mean == doctest::Approx(3.0));
    CHECK(pm.summary.median == doctest::Approx(3.0));
    CHECK(pm.summary.p95 == doctest::Approx(4.5));
    CHECK(pm.summary.p99 == doctest::Approx(4.9));
    CHECK(pm.summary.stddev == doctest::Approx(1.2));
    CHECK(pm.summary.count == 100);
    CHECK(pm.summary.valid);
    CHECK(pm.rawSamples.size() == 5);
    CHECK(pm.rawSamples[0] == doctest::Approx(1.0));
    CHECK(pm.rawSamples[4] == doctest::Approx(5.0));
}

TEST_CASE("BenchmarkResult: JSON round-trip (metadata)")
{
    BenchmarkResult result;
    result.source.commitHash = "abc123def";
    result.source.dirty = true;
    result.source.diffHash = "deadbeef";
    result.gpu.deviceName = "AMD Radeon RX 7900 XTX";
    result.gpu.vendorId = 4098;
    result.gpu.driverVersion = "26.2.6";
    result.gpu.vulkanApiVersion = "1.4.354";
    result.gpu.timestampValidBits = 64;
    result.gpu.timestampPeriodNs = 10.0;
    result.cpu.model = "AMD Ryzen 9 7950X";
    result.cpu.coreCount = 16;
    result.cpu.threadCount = 32;
    result.cpu.governor = "performance";
    result.system.os = "Linux";
    result.system.kernel = "6.12.0";
    result.system.ramBytes = 68719476736ULL;
    result.build.compiler = "Clang 23.1.4";
    result.build.buildType = "Release";
    result.build.flags = "-O3 -DNDEBUG";
    result.build.validationEnabled = false;
    result.workload.sceneId = "submission_stress";
    result.workload.seed = 42;
    result.workload.instanceCount = 100000;
    result.workload.visibleFraction = 0.1;
    result.workload.resolutionWidth = 1920;
    result.workload.resolutionHeight = 1080;
    result.memory.allocatedBytes = 104857600;
    result.memory.usedBytes = 83886080;
    result.memory.allocationCount = 42;
    result.memory.blockCount = 8;
    result.environment.gpuClockMhz = 2500.0;
    result.environment.gpuTempC = 65.0;
    result.environment.cpuFreqMhz = std::nullopt;

    std::string json = result.toJson();
    auto parsed = BenchmarkResult::fromJson(json);
    REQUIRE(parsed.has_value());

    CHECK(parsed->source.commitHash == "abc123def");
    CHECK(parsed->source.dirty);
    CHECK(parsed->source.diffHash.has_value());
    CHECK(parsed->source.diffHash.value() == "deadbeef");
    CHECK(parsed->gpu.deviceName == "AMD Radeon RX 7900 XTX");
    CHECK(parsed->gpu.vendorId == 4098);
    CHECK(parsed->gpu.driverVersion == "26.2.6");
    CHECK(parsed->cpu.model == "AMD Ryzen 9 7950X");
    CHECK(parsed->cpu.coreCount == 16);
    CHECK(parsed->system.os == "Linux");
    CHECK(parsed->system.ramBytes == 68719476736ULL);
    CHECK(parsed->build.compiler == "Clang 23.1.4");
    CHECK(parsed->build.buildType == "Release");
    CHECK(parsed->build.validationEnabled == false);
    CHECK(parsed->workload.sceneId == "submission_stress");
    CHECK(parsed->workload.seed == 42);
    CHECK(parsed->workload.instanceCount == 100000);
    CHECK(parsed->workload.visibleFraction == doctest::Approx(0.1));
    CHECK(parsed->memory.allocatedBytes == 104857600);
    CHECK(parsed->memory.allocationCount == 42);
    CHECK(parsed->environment.gpuClockMhz.has_value());
    CHECK(parsed->environment.gpuClockMhz.value() == doctest::Approx(2500.0));
    CHECK_FALSE(parsed->environment.cpuFreqMhz.has_value());
}

TEST_CASE("BenchmarkResult: JSON round-trip (null diff hash)")
{
    BenchmarkResult result;
    result.source.diffHash = std::nullopt;

    std::string json = result.toJson();
    auto parsed = BenchmarkResult::fromJson(json);
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->source.diffHash.has_value());
}

TEST_CASE("BenchmarkResult: invalid JSON returns error")
{
    auto parsed = BenchmarkResult::fromJson("{ not valid json }");
    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error().find("JSON parse error") != std::string::npos);
}

TEST_CASE("BenchmarkResult: empty JSON string")
{
    auto parsed = BenchmarkResult::fromJson("");
    CHECK_FALSE(parsed.has_value());
}

TEST_CASE("BenchmarkResult: missing fields use defaults")
{
    auto parsed = BenchmarkResult::fromJson("{}");
    REQUIRE(parsed.has_value());
    CHECK(parsed->schemaVersion == kSchemaVersion);
    CHECK(parsed->benchmarkId.empty());
    CHECK(parsed->metrics.empty());
    CHECK(parsed->memory.allocatedBytes == 0);
}
