#include <doctest/doctest.h>

#include "bench/benchmark_result.hpp"
#include "bench/budget_compare.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#ifndef STARFORGE_BUDGETS_FILE
#define STARFORGE_BUDGETS_FILE "benchmarks/budgets.json"
#endif

using namespace starforge::bench;

namespace
{

BudgetDefinition loadCommittedBudgets()
{
    auto budgets = loadBudgets(STARFORGE_BUDGETS_FILE);
    REQUIRE(budgets.has_value());
    return *budgets;
}

BudgetLine findLine(const BudgetDefinition& budgets, const std::string& name)
{
    for (const auto& line : budgets.lines)
    {
        if (line.name == name) return line;
    }
    return {};
}

BudgetComparisonRow findRow(const BudgetComparison& comparison, const std::string& name)
{
    for (const auto& row : comparison.rows)
    {
        if (row.line.name == name) return row;
    }
    return {};
}

BenchmarkResult makeBaseline(const std::string& metric, double p95, const std::string& gpu = "AMD Radeon RX 7900 XTX")
{
    BenchmarkResult result;
    result.benchmarkId = "test_baseline";
    result.workloadId = "submission_stress";
    result.gpu.deviceName = gpu;
    Metric m;
    m.summary.p95 = p95;
    m.summary.median = p95;
    m.summary.valid = true;
    result.metrics[metric] = m;
    return result;
}

} // namespace

TEST_CASE("Budget compare: committed budgets.json loads with all 4.4a elements")
{
    const auto budgets = loadCommittedBudgets();
    CHECK(budgets.schemaVersion == 1);
    CHECK(budgets.targetFrameIntervalMs == doctest::Approx(6.94));
    CHECK(budgets.statistic == "p95");
    CHECK(budgets.resolutionWidth == 1920);
    CHECK(budgets.resolutionHeight == 1080);
    CHECK_FALSE(budgets.referenceScene.empty());
    CHECK(budgets.headroomPercent == doctest::Approx(20.0));
    CHECK_FALSE(budgets.hardwareGpu.empty());

    // Frame interval + 3 CPU + 4 GPU + 2 memory lines.
    CHECK(budgets.lines.size() == 10);
    CHECK(findLine(budgets, "frame_interval").metric == "frame_time_ms");
    CHECK(findLine(budgets, "draw_submission").budgetValue == doctest::Approx(0.80));
    CHECK(findLine(budgets, "draw_submission").metric == "cpu_submission_ms");
    CHECK(findLine(budgets, "main_geometry_pass").metric.empty()); // needs per-pass timestamps
    CHECK(findLine(budgets, "vram_max_bytes").unit == "bytes");
    CHECK(findLine(budgets, "vram_max_bytes").budgetValue == doctest::Approx(4294967296.0));
}

TEST_CASE("Budget compare: missing and malformed files are rejected")
{
    CHECK_FALSE(loadBudgets("does/not/exist/budgets.json").has_value());

    const auto temp = std::filesystem::temp_directory_path() / "starforge_bad_budgets.json";
    {
        std::ofstream out(temp);
        out << "{ this is not JSON";
    }
    CHECK_FALSE(loadBudgets(temp.string()).has_value());
    std::filesystem::remove(temp);
}

TEST_CASE("Budget compare: no baseline yields unknown rows with owning milestones")
{
    const auto budgets = loadCommittedBudgets();
    const auto comparison = compareBudgets(budgets, std::nullopt);

    CHECK(comparison.rows.size() == budgets.lines.size());
    CHECK(comparison.metCount() == 0);
    CHECK(comparison.overCount() == 0);
    CHECK(comparison.unknownCount() == budgets.lines.size());
    CHECK_FALSE(comparison.hardwareMatches);
    CHECK_FALSE(comparison.hardwareMismatchReason.empty());

    const auto draw = findRow(comparison, "draw_submission");
    CHECK(std::string(toString(draw.status)) == "unknown");
    CHECK_FALSE(draw.measuredValue.has_value());
    CHECK(draw.margin == doctest::Approx(0.0));
    CHECK(draw.line.ownerMilestone == "M4.3");

    // Every unmeasured line names the milestone that addresses its gap.
    for (const auto& row : comparison.rows)
    {
        CHECK_FALSE(row.line.ownerMilestone.empty());
    }
}

TEST_CASE("Budget compare: measured metric within budget is met with positive margin")
{
    const auto budgets = loadCommittedBudgets();
    const auto comparison = compareBudgets(budgets, makeBaseline("cpu_submission_ms", 0.5));

    const auto draw = findRow(comparison, "draw_submission");
    CHECK(std::string(toString(draw.status)) == "met");
    REQUIRE(draw.measuredValue.has_value());
    CHECK(*draw.measuredValue == doctest::Approx(0.5));
    CHECK(draw.margin == doctest::Approx(0.30));
    CHECK(comparison.metCount() == 1);
}

TEST_CASE("Budget compare: measured metric above budget is over with negative margin")
{
    const auto budgets = loadCommittedBudgets();
    const auto comparison = compareBudgets(budgets, makeBaseline("cpu_submission_ms", 1.2));

    const auto draw = findRow(comparison, "draw_submission");
    CHECK(std::string(toString(draw.status)) == "over");
    CHECK(draw.margin == doctest::Approx(-0.40));
    CHECK(findRow(comparison, "draw_submission").line.ownerMilestone == "M4.3");
    CHECK(comparison.overCount() == 1);
}

TEST_CASE("Budget compare: per-pass GPU lines stay unknown; frame interval is measured")
{
    const auto budgets = loadCommittedBudgets();
    auto baseline = makeBaseline("gpu_time_ms", 3.0); // whole-frame, not a pass
    Metric frameM;
    frameM.summary.p95 = 8.0;
    frameM.summary.valid = true;
    baseline.metrics["frame_time_ms"] = frameM;

    const auto comparison = compareBudgets(budgets, baseline);
    CHECK(std::string(toString(findRow(comparison, "main_geometry_pass").status)) == "unknown");
    CHECK(std::string(toString(findRow(comparison, "frame_interval").status)) == "over");
    CHECK(comparison.overCount() == 1);
}

TEST_CASE("Budget compare: hardware mismatch is reported when GPU differs")
{
    const auto budgets = loadCommittedBudgets();

    auto matchingBaseline = makeBaseline("cpu_submission_ms", 0.4);
    matchingBaseline.cpu.model = budgets.hardwareCpu;
    const auto matching = compareBudgets(budgets, matchingBaseline);
    CHECK(matching.hardwareMatches);
    CHECK(matching.hardwareMismatchReason.empty());

    const auto mismatched = compareBudgets(budgets, makeBaseline("cpu_submission_ms", 0.4, "NVIDIA GeForce RTX 4090"));
    CHECK_FALSE(mismatched.hardwareMatches);
    CHECK(mismatched.hardwareMismatchReason.find("does not match") != std::string::npos);
}

TEST_CASE("Budget compare: report has one row per budget line and all statuses")
{
    const auto budgets = loadCommittedBudgets();
    auto baseline = makeBaseline("cpu_submission_ms", 1.2);
    baseline.benchmarkId = "competent_10k";
    const auto comparison = compareBudgets(budgets, baseline);
    const std::string report = renderBudgetReport(comparison);

    CHECK(report.find("Baseline vs. budget comparison") != std::string::npos);
    CHECK(report.find("competent_10k") != std::string::npos);
    CHECK(report.find("met, 1 over") != std::string::npos);
    CHECK(report.find("| draw_submission | cpu |") != std::string::npos);
    CHECK(report.find("| frustum_culling | cpu |") != std::string::npos);
    CHECK(report.find("unknown") != std::string::npos);
    CHECK(report.find("M4.3") != std::string::npos);

    std::size_t tableRows = 0;
    std::size_t pos = 0;
    while ((pos = report.find('\n', pos)) != std::string::npos)
    {
        ++pos;
        if (report.compare(pos, 2, "| ") == 0) ++tableRows;
    }
    // Header + one row per line (the separator starts with "|---", not "| ").
    CHECK(tableRows == comparison.rows.size() + 1);
}

TEST_CASE("Budget compare: report writes to disk and round-trips")
{
    const auto budgets = loadCommittedBudgets();
    const auto comparison = compareBudgets(budgets, std::nullopt);
    const auto temp = std::filesystem::temp_directory_path() / "starforge_budget_report.md";

    const auto written = writeBudgetReport(comparison, temp.string());
    REQUIRE(written.has_value());

    std::ifstream in(temp);
    REQUIRE(in.good());
    const std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(contents == renderBudgetReport(comparison));
    std::filesystem::remove(temp);
}
