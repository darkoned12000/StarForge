#include <doctest/doctest.h>

#include "bench/benchmark_compare.hpp"
#include "bench/benchmark_result.hpp"
#include "bench/budget_compare.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef STARFORGE_M5_BASELINE_DIR
#define STARFORGE_M5_BASELINE_DIR "benchmarks/baselines"
#endif
#ifndef STARFORGE_M5_REPORT_FILE
#define STARFORGE_M5_REPORT_FILE "benchmarks/m5-vs-budget.md"
#endif

using namespace starforge::bench;

namespace
{

BenchmarkResult load(const std::string& name)
{
    std::ifstream in(std::string(STARFORGE_M5_BASELINE_DIR) + "/" + name + ".json");
    REQUIRE(in.good());
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto result = BenchmarkResult::fromJson(json);
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

TEST_CASE("M5 Phase 5: committed baselines show GPU-driven beating CPU-driven")
{
    for (const char* count : {"100000", "1000000"})
    {
        const auto m4 = load(std::string("cpu_naive_") + count);
        const auto m5 = load(std::string("gpu_driven_") + count);

        const auto comparison = compare(m4, m5);
        CHECK_FALSE(comparison.hasIncompatibleWorkload);
        const double m4Frame = m4.metrics.at("frame_time_ms").summary.median;
        const double m5Frame = m5.metrics.at("frame_time_ms").summary.median;
        CHECK(m5Frame < m4Frame);
        CHECK(m5.metrics.at("cpu_submission_ms").summary.median <= 0.50);
    }
}

TEST_CASE("M5 Phase 5: committed M5 budget report is consistent with ADR-004")
{
#ifndef STARFORGE_BUDGETS_FILE
#define STARFORGE_BUDGETS_FILE "benchmarks/budgets.json"
#endif
    auto budgets = loadBudgets(STARFORGE_BUDGETS_FILE);
    REQUIRE(budgets.has_value());
    const auto m5 = load("gpu_driven_100000");
    const auto comparison = compareBudgets(*budgets, m5);

    CHECK(comparison.baselineId == "gpu_driven_100000");
    CHECK(comparison.metCount() >= 2);
    CHECK(comparison.overCount() == 0);
    CHECK(comparison.rows.size() == 10);

    std::ifstream in(STARFORGE_M5_REPORT_FILE);
    REQUIRE(in.good());
    const std::string report{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(report.find("gpu_driven_100000") != std::string::npos);
    CHECK(report.find("0 over") != std::string::npos);
    CHECK(report.find("| frame_interval") != std::string::npos);
}
