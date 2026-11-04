#include <doctest/doctest.h>

#include "stats/statistics.hpp"

#include <cmath>
#include <limits>
#include <vector>

using namespace starforge::stats;

namespace
{

bool approxEqual(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) < eps;
}

} // namespace

TEST_CASE("Statistics: empty input returns invalid")
{
    std::vector<double> empty;
    auto stats = compute(empty);
    CHECK_FALSE(stats.valid);
    CHECK(stats.count == 0);
}

TEST_CASE("Statistics: single sample")
{
    std::vector<double> samples = {42.0};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 1);
    CHECK(approxEqual(stats.min, 42.0));
    CHECK(approxEqual(stats.max, 42.0));
    CHECK(approxEqual(stats.mean, 42.0));
    CHECK(approxEqual(stats.median, 42.0));
    CHECK(approxEqual(stats.p95, 42.0));
    CHECK(approxEqual(stats.p99, 42.0));
    CHECK(approxEqual(stats.stddev, 0.0)); // sample stddev undefined for n=1, set to 0
}

TEST_CASE("Statistics: two samples")
{
    std::vector<double> samples = {10.0, 20.0};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 2);
    CHECK(approxEqual(stats.min, 10.0));
    CHECK(approxEqual(stats.max, 20.0));
    CHECK(approxEqual(stats.mean, 15.0));
    CHECK(approxEqual(stats.median, 15.0));
    // Sample stddev = sqrt(((10-15)^2 + (20-15)^2) / (2-1)) = sqrt(50) = 7.071
    CHECK(approxEqual(stats.stddev, std::sqrt(50.0)));
}

TEST_CASE("Statistics: identical samples")
{
    std::vector<double> samples = {5.0, 5.0, 5.0, 5.0, 5.0};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 5);
    CHECK(approxEqual(stats.min, 5.0));
    CHECK(approxEqual(stats.max, 5.0));
    CHECK(approxEqual(stats.mean, 5.0));
    CHECK(approxEqual(stats.median, 5.0));
    CHECK(approxEqual(stats.p95, 5.0));
    CHECK(approxEqual(stats.p99, 5.0));
    CHECK(approxEqual(stats.stddev, 0.0));
}

TEST_CASE("Statistics: known values")
{
    // 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    std::vector<double> samples;
    for (int i = 1; i <= 10; ++i) samples.push_back(static_cast<double>(i));
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 10);
    CHECK(approxEqual(stats.min, 1.0));
    CHECK(approxEqual(stats.max, 10.0));
    CHECK(approxEqual(stats.mean, 5.5));
    CHECK(approxEqual(stats.median, 5.5)); // (5+6)/2
    // Sample stddev for 1..10: sqrt(9.1667) ≈ 3.02765
    CHECK(approxEqual(stats.stddev, 3.027650354, 1e-6));
}

TEST_CASE("Statistics: outliers")
{
    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 100.0};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 5);
    CHECK(approxEqual(stats.min, 1.0));
    CHECK(approxEqual(stats.max, 100.0));
    CHECK(approxEqual(stats.median, 3.0));
}

TEST_CASE("Statistics: non-finite values filtered")
{
    std::vector<double> samples = {1.0, 2.0, std::numeric_limits<double>::quiet_NaN(),
                                    3.0, std::numeric_limits<double>::infinity(),
                                    4.0, -std::numeric_limits<double>::infinity()};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 4); // only finite values
    CHECK(approxEqual(stats.min, 1.0));
    CHECK(approxEqual(stats.max, 4.0));
    CHECK(approxEqual(stats.mean, 2.5));
}

TEST_CASE("Statistics: all non-finite returns invalid")
{
    std::vector<double> samples = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
    };
    auto stats = compute(samples);
    CHECK_FALSE(stats.valid);
    CHECK(stats.count == 0);
}

TEST_CASE("Statistics: percentile method (linear interpolation)")
{
    std::vector<double> sorted = {1.0, 2.0, 3.0, 4.0, 5.0};

    // p=0 → min
    CHECK(approxEqual(percentile(sorted, 0.0), 1.0));
    // p=100 → max
    CHECK(approxEqual(percentile(sorted, 100.0), 5.0));
    // p=50 → median
    CHECK(approxEqual(percentile(sorted, 50.0), 3.0));
    // p=25 → between 1 and 2 (index = 0.25*4 = 1.0, exactly index 1 → value 2)
    // Actually: index = 0.25 * 4 = 1.0, lo=1, hi=1, frac=0 → sorted[1] = 2
    CHECK(approxEqual(percentile(sorted, 25.0), 2.0));
    // p=75 → index = 0.75 * 4 = 3.0 → sorted[3] = 4
    CHECK(approxEqual(percentile(sorted, 75.0), 4.0));

    // Interpolation: p=10 → index = 0.1 * 4 = 0.4 → 1 + 0.4*(2-1) = 1.4
    CHECK(approxEqual(percentile(sorted, 10.0), 1.4));

    // Empty data → NaN
    std::vector<double> empty;
    CHECK(std::isnan(percentile(empty, 50.0)));

    // Single element
    std::vector<double> single = {42.0};
    CHECK(approxEqual(percentile(single, 0.0), 42.0));
    CHECK(approxEqual(percentile(single, 50.0), 42.0));
    CHECK(approxEqual(percentile(single, 100.0), 42.0));

    // Clamp p
    CHECK(approxEqual(percentile(sorted, -10.0), 1.0));
    CHECK(approxEqual(percentile(sorted, 200.0), 5.0));

    // NaN p returns NaN
    CHECK(std::isnan(percentile(sorted, std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("Statistics: large sample set")
{
    std::vector<double> samples(100000);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<double>(i);
    }
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 100000);
    CHECK(approxEqual(stats.min, 0.0));
    CHECK(approxEqual(stats.max, 99999.0));
    CHECK(approxEqual(stats.mean, 49999.5));
    // Median of 0..99999 = 49999.5
    CHECK(approxEqual(stats.median, 49999.5));
}

TEST_CASE("Statistics: float samples")
{
    std::vector<float> samples = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    auto stats = compute(samples);
    CHECK(stats.valid);
    CHECK(stats.count == 5);
    CHECK(approxEqual(stats.mean, 3.0));
    CHECK(approxEqual(stats.median, 3.0));
}

TEST_CASE("Statistics: p95 and p99 for 100 samples")
{
    std::vector<double> samples;
    for (int i = 1; i <= 100; ++i) samples.push_back(static_cast<double>(i));
    auto stats = compute(samples);
    CHECK(stats.valid);
    // p95 with linear interpolation: index = 0.95 * 99 = 94.05 → 95 + 0.05*(96-95) = 95.05
    CHECK(approxEqual(stats.p95, 95.05, 1e-6));
    // p99: index = 0.99 * 99 = 98.01 → 99 + 0.01*(100-99) = 99.01
    CHECK(approxEqual(stats.p99, 99.01, 1e-6));
}
