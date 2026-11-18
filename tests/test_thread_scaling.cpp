#include <doctest/doctest.h>

#include "bench/thread_scaling.hpp"

#include <cstdint>

using namespace starforge::bench;

namespace
{

ThreadScalingConfig smallConfig()
{
    ThreadScalingConfig config;
    config.threadCounts = {1, 2, 4};
    config.batchCount = 4;
    config.cellsPerBatch = 8;
    config.heightSamples = 2000;
    config.repeats = 2;
    return config;
}

} // namespace

TEST_CASE("Scaling benchmark: invalid configurations are rejected")
{
    auto config = smallConfig();
    config.threadCounts.clear();
    CHECK_FALSE(runThreadScalingBenchmark(config).has_value());

    config = smallConfig();
    config.repeats = 0;
    CHECK_FALSE(runThreadScalingBenchmark(config).has_value());

    config = smallConfig();
    config.threadCounts = {1, 0};
    CHECK_FALSE(runThreadScalingBenchmark(config).has_value());
}

TEST_CASE("Scaling benchmark: all thread counts produce bit-identical results")
{
    auto result = runThreadScalingBenchmark(smallConfig());
    REQUIRE(result.has_value());

    CHECK(result->schemaVersion == 1);
    CHECK(result->samples.size() == 3);
    CHECK(result->samples[0].threadCount == 1);
    CHECK(result->samples[1].threadCount == 2);
    CHECK(result->samples[2].threadCount == 4);

    CHECK(result->samples[0].speedup == doctest::Approx(1.0));
    CHECK(result->samples[0].bitIdentical);
    CHECK(result->samples[1].bitIdentical);
    CHECK(result->samples[2].bitIdentical);
    CHECK(result->samples[0].checksum != 0);

    for (const auto& sample : result->samples)
    {
        CHECK(sample.checksum == result->samples[0].checksum);
        CHECK(sample.medianMs > 0.0);
        CHECK(sample.bestMs > 0.0);
        CHECK(sample.bestMs <= sample.medianMs);
    }
}

TEST_CASE("Scaling benchmark: JSON round-trips samples and workload config")
{
    auto result = runThreadScalingBenchmark(smallConfig());
    REQUIRE(result.has_value());

    const std::string json = result->toJson();
    auto parsed = ThreadScalingResult::fromJson(json);
    REQUIRE(parsed.has_value());

    CHECK(parsed->schemaVersion == 1);
    CHECK(parsed->config.batchCount == 4);
    CHECK(parsed->config.cellsPerBatch == 8);
    CHECK(parsed->samples.size() == result->samples.size());
    for (std::size_t i = 0; i < result->samples.size(); ++i)
    {
        CHECK(parsed->samples[i].threadCount == result->samples[i].threadCount);
        CHECK(parsed->samples[i].checksum == result->samples[i].checksum);
        CHECK(parsed->samples[i].medianMs == doctest::Approx(result->samples[i].medianMs));
        CHECK(parsed->samples[i].bitIdentical == result->samples[i].bitIdentical);
    }

    CHECK_FALSE(ThreadScalingResult::fromJson("{ not json").has_value());
}
