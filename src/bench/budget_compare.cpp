#include "bench/budget_compare.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <utility>

namespace starforge::bench
{

using json = nlohmann::json;

namespace
{

template <typename T>
T valueOr(const json& j, const char* key, const T& fallback)
{
    if (j.is_object() && j.contains(key) && !j[key].is_null())
    {
        return j[key].get<T>();
    }
    return fallback;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string formatMeasured(double value, const std::string& unit)
{
    if (unit == "bytes" || unit == "bytes/s")
    {
        return std::format("{:.0f} {}", value, unit);
    }
    return std::format("{:.3f} {}", value, unit);
}

} // namespace

const char* toString(BudgetStatus status) noexcept
{
    switch (status)
    {
    case BudgetStatus::Met:     return "met";
    case BudgetStatus::Over:    return "over";
    case BudgetStatus::Unknown: return "unknown";
    }
    return "unknown";
}

std::size_t BudgetComparison::metCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
                                                  [](const auto& r) { return r.status == BudgetStatus::Met; }));
}

std::size_t BudgetComparison::overCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
                                                  [](const auto& r) { return r.status == BudgetStatus::Over; }));
}

std::size_t BudgetComparison::unknownCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
                                                  [](const auto& r) { return r.status == BudgetStatus::Unknown; }));
}

std::expected<BudgetDefinition, std::string> loadBudgets(const std::string& filePath)
{
    std::ifstream in(filePath);
    if (!in)
    {
        return std::unexpected("cannot open budgets file: " + filePath);
    }

    json j;
    try
    {
        in >> j;
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("invalid budgets JSON: ") + e.what());
    }
    if (!j.is_object())
    {
        return std::unexpected("budgets root is not a JSON object");
    }

    BudgetDefinition budgets;
    budgets.schemaVersion = valueOr<std::uint32_t>(j, "schema_version", 0);
    budgets.targetFrameIntervalMs = valueOr<double>(j, "target_frame_interval_ms", 0.0);
    budgets.statistic = valueOr<std::string>(j, "statistic", "p95");
    budgets.referenceScene = valueOr<std::string>(j, "reference_scene", "");
    budgets.headroomPercent = valueOr<double>(j, "headroom_percent", 0.0);

    if (j.contains("resolution") && j["resolution"].is_object())
    {
        budgets.resolutionWidth = valueOr<std::uint32_t>(j["resolution"], "width", 0);
        budgets.resolutionHeight = valueOr<std::uint32_t>(j["resolution"], "height", 0);
    }
    if (j.contains("hardware") && j["hardware"].is_object())
    {
        budgets.hardwareGpu = valueOr<std::string>(j["hardware"], "gpu", "");
        budgets.hardwareDriver = valueOr<std::string>(j["hardware"], "driver", "");
        budgets.hardwareCpu = valueOr<std::string>(j["hardware"], "cpu", "");
        budgets.hardwareNote = valueOr<std::string>(j["hardware"], "note", "");
    }

    // Timing lines: every object-valued entry with a "budget" field is a line.
    auto readTimingSection = [&](const char* key, const char* side) {
        if (!j.contains(key) || !j[key].is_object()) return;
        for (auto it = j[key].begin(); it != j[key].end(); ++it)
        {
            const json& v = it.value();
            if (!v.is_object() || !v.contains("budget")) continue;

            BudgetLine line;
            line.name = it.key();
            line.side = side;
            line.budgetValue = valueOr<double>(v, "budget", 0.0);
            line.unit = "ms";
            line.appliesTo = valueOr<std::string>(v, "applies_to", "");
            line.metric = valueOr<std::string>(v, "metric", "");
            line.ownerMilestone = valueOr<std::string>(v, "owner_milestone", "");
            line.notes = valueOr<std::string>(v, "notes", "");
            budgets.lines.push_back(std::move(line));
        }
    };
    readTimingSection("cpu_budgets_ms", "cpu");
    readTimingSection("gpu_budgets_ms", "gpu");

    // Memory lines: scalar values with native units, plus optional metric
    // overrides (measured metrics may be added later without changing schema).
    if (j.contains("memory_budgets") && j["memory_budgets"].is_object())
    {
        const json& mem = j["memory_budgets"];
        auto addMemoryLine = [&](const char* key, const char* unit) {
            if (!mem.contains(key) || mem[key].is_null()) return;
            BudgetLine line;
            line.name = key;
            line.side = "memory";
            line.budgetValue = mem[key].get<double>();
            line.unit = unit;
            line.appliesTo = "process";
            line.notes = valueOr<std::string>(mem, "upload_note", "");
            if (mem.contains("metric_overrides") && mem["metric_overrides"].is_object() &&
                mem["metric_overrides"].contains(key))
            {
                const json& ov = mem["metric_overrides"][key];
                line.metric = valueOr<std::string>(ov, "metric", "");
                line.ownerMilestone = valueOr<std::string>(ov, "owner_milestone", "");
                const std::string overrideUnit = valueOr<std::string>(ov, "unit", "");
                if (!overrideUnit.empty()) line.unit = overrideUnit;
            }
            budgets.lines.push_back(std::move(line));
        };
        addMemoryLine("vram_max_bytes", "bytes");
        addMemoryLine("upload_bandwidth_max_bytes_per_sec", "bytes/s");
    }

    // Top-level frame interval is itself a budget line (4.4a target).
    BudgetLine frame;
    frame.name = "frame_interval";
    frame.side = "frame";
    frame.budgetValue = budgets.targetFrameIntervalMs;
    frame.unit = "ms";
    frame.appliesTo = "whole frame";
    frame.metric = "frame_time_ms";
    frame.ownerMilestone = "M4.3";
    frame.notes = "ADR-004 target frame interval (p95)";
    budgets.lines.insert(budgets.lines.begin(), std::move(frame));

    if (budgets.lines.size() < 2)
    {
        return std::unexpected("budgets file has no budget lines");
    }
    return budgets;
}

BudgetComparison compareBudgets(const BudgetDefinition& budgets,
                                const std::optional<BenchmarkResult>& baseline)
{
    BudgetComparison comparison;
    comparison.budgets = budgets;
    comparison.baselineId = baseline ? baseline->benchmarkId : std::string("(no baseline supplied)");

    if (baseline)
    {
        const std::string actualGpu = lower(baseline->gpu.deviceName);
        const std::string referenceGpu = lower(budgets.hardwareGpu);
        const std::string actualCpu = lower(baseline->cpu.model);
        const std::string referenceCpu = lower(budgets.hardwareCpu);

        if (referenceGpu.empty() || actualGpu.find(referenceGpu) == std::string::npos)
        {
            comparison.hardwareMismatchReason =
                "Baseline GPU '" + baseline->gpu.deviceName + "' does not match budget reference '" +
                budgets.hardwareGpu + "' — absolute budgets do not transfer";
        }
        else if (!referenceCpu.empty() &&
                 (actualCpu.empty() || actualCpu.find(referenceCpu) == std::string::npos))
        {
            comparison.hardwareMismatchReason =
                "Baseline CPU '" + baseline->cpu.model + "' does not match budget reference '" +
                budgets.hardwareCpu + "' — CPU-side budgets may not transfer";
        }
        else
        {
            comparison.hardwareMatches = true;
        }
    }
    else
    {
        comparison.hardwareMismatchReason = "No baseline supplied; all measured values are unknown";
    }

    for (const BudgetLine& line : budgets.lines)
    {
        BudgetComparisonRow row;
        row.line = line;

        if (baseline && !line.metric.empty())
        {
            const auto it = baseline->metrics.find(line.metric);
            if (it != baseline->metrics.end() && it->second.summary.valid)
            {
                // ADR-004 selects p95 as the budget statistic.
                row.measuredValue = it->second.summary.p95;
                row.margin = line.budgetValue - *row.measuredValue;
                row.status = (*row.measuredValue <= line.budgetValue) ? BudgetStatus::Met : BudgetStatus::Over;
            }
        }
        comparison.rows.push_back(std::move(row));
    }
    return comparison;
}

std::string renderBudgetReport(const BudgetComparison& comparison)
{
    std::string out;
    out += "# Baseline vs. budget comparison (4.4b)\n\n";
    out += std::format("- Budgets: ADR-004 `benchmarks/budgets.json` — {}, {}×{}, interval {:.2f} ms, {}% headroom\n",
                       comparison.budgets.statistic,
                       comparison.budgets.resolutionWidth, comparison.budgets.resolutionHeight,
                       comparison.budgets.targetFrameIntervalMs, comparison.budgets.headroomPercent);
    out += std::format("- Baseline: `{}`\n", comparison.baselineId);
    out += std::format("- Hardware scope: {} / {} — {}\n",
                       comparison.budgets.hardwareGpu, comparison.budgets.hardwareCpu,
                       comparison.budgets.hardwareNote);
    out += std::format("- Hardware match: {}\n",
                       comparison.hardwareMatches ? "yes" : "no — " + comparison.hardwareMismatchReason);
    out += std::format("- Summary: {} met, {} over, {} unknown ({} lines)\n\n",
                       comparison.metCount(), comparison.overCount(), comparison.unknownCount(),
                       comparison.rows.size());

    out += "| Budget line | Side | Budget | Measured (p95) | Status | Margin | Milestone | Notes |\n";
    out += "|---|---|---|---|---|---|---|---|\n";
    for (const BudgetComparisonRow& row : comparison.rows)
    {
        const std::string measured = row.measuredValue
                                         ? formatMeasured(*row.measuredValue, row.line.unit)
                                         : std::string("—");
        const std::string margin = row.measuredValue
                                       ? formatMeasured(row.margin, row.line.unit)
                                       : std::string("—");
        const std::string milestone = row.status == BudgetStatus::Met
                                          ? std::string("—")
                                          : (row.line.ownerMilestone.empty() ? std::string("unassigned")
                                                                             : row.line.ownerMilestone);
        out += std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n",
                           row.line.name, row.line.side,
                           formatMeasured(row.line.budgetValue, row.line.unit),
                           measured, toString(row.status), margin, milestone, row.line.notes);
    }
    return out;
}

std::expected<void, std::string> writeBudgetReport(const BudgetComparison& comparison,
                                                   const std::string& filePath)
{
    std::filesystem::path path(filePath);
    if (!path.parent_path().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return std::unexpected("cannot create report directory: " + ec.message());
    }
    std::ofstream out(filePath, std::ios::trunc);
    if (!out)
    {
        return std::unexpected("cannot open report for writing: " + filePath);
    }
    out << renderBudgetReport(comparison);
    if (!out)
    {
        return std::unexpected("failed while writing report: " + filePath);
    }
    return {};
}

} // namespace starforge::bench
