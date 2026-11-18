#pragma once

#include "bench/benchmark_result.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace starforge::bench
{

// A single budget line from benchmarks/budgets.json (4.4a). `metric` names the
// BenchmarkResult metric that measures this line; an empty metric means the
// line is not measured by the current harness and is reported as "unknown".
struct BudgetLine
{
    std::string name;
    std::string side; // "frame", "cpu", "gpu", or "memory"
    double      budgetValue = 0.0;
    std::string unit = "ms";
    std::string appliesTo;
    std::string metric;
    std::string ownerMilestone; // milestone that addresses a gap on this line
    std::string notes;
};

struct BudgetDefinition
{
    std::uint32_t schemaVersion = 0;
    double        targetFrameIntervalMs = 0.0;
    std::string   statistic;
    std::uint32_t resolutionWidth = 0;
    std::uint32_t resolutionHeight = 0;
    std::string   referenceScene;
    double        headroomPercent = 0.0;
    std::string   hardwareGpu;
    std::string   hardwareDriver;
    std::string   hardwareCpu;
    std::string   hardwareNote;
    std::vector<BudgetLine> lines;
};

enum class BudgetStatus
{
    Met,
    Over,
    Unknown,
};

[[nodiscard]] const char* toString(BudgetStatus status) noexcept;

// One row of the committed baseline-vs-budget comparison (4.4b).
struct BudgetComparisonRow
{
    BudgetLine          line;
    std::optional<double> measuredValue;
    BudgetStatus        status = BudgetStatus::Unknown;
    double              margin = 0.0; // budget - measured; zero when unknown
};

struct BudgetComparison
{
    BudgetDefinition        budgets;
    std::vector<BudgetComparisonRow> rows;
    std::string             baselineId;
    bool                    hardwareMatches = false;
    std::string             hardwareMismatchReason;

    [[nodiscard]] std::size_t metCount() const noexcept;
    [[nodiscard]] std::size_t overCount() const noexcept;
    [[nodiscard]] std::size_t unknownCount() const noexcept;
};

// Load the machine-readable budget definition. Malformed JSON or a missing
// file returns an error with the reason.
[[nodiscard]] std::expected<BudgetDefinition, std::string>
loadBudgets(const std::string& filePath);

// Compare a budget definition against an optional committed baseline. Without
// a baseline every unmeasured line is "unknown"; mapped metrics are evaluated
// with the budget's statistic (p95).
[[nodiscard]] BudgetComparison
compareBudgets(const BudgetDefinition& budgets,
               const std::optional<BenchmarkResult>& baseline);

// Render the comparison as a Markdown table with one row per budget line.
[[nodiscard]] std::string renderBudgetReport(const BudgetComparison& comparison);

[[nodiscard]] std::expected<void, std::string>
writeBudgetReport(const BudgetComparison& comparison, const std::string& filePath);

} // namespace starforge::bench
