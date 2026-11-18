// 4.4b: generate the committed baseline-vs-budget comparison report.
//
// Usage: starforge_bench_report <budgets.json> <baseline.json> <output.md>
// A baseline path of "-" produces a report where every mapped metric is
// "unknown" (useful when no baseline has been measured yet).

#include "bench/benchmark_result.hpp"
#include "bench/budget_compare.hpp"

#include <core/log.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: starforge_bench_report <budgets.json> <baseline.json|-> <output.md>\n";
        return EXIT_FAILURE;
    }

    const std::string budgetsPath = argv[1];
    const std::string baselinePath = argv[2];
    const std::string reportPath = argv[3];

    auto budgets = starforge::bench::loadBudgets(budgetsPath);
    if (!budgets)
    {
        starforge::core::logError("budget", "{}", budgets.error());
        return EXIT_FAILURE;
    }

    std::optional<starforge::bench::BenchmarkResult> baseline;
    if (baselinePath != "-")
    {
        std::ifstream in(baselinePath);
        if (!in)
        {
            starforge::core::logError("budget", "cannot open baseline: {}", baselinePath);
            return EXIT_FAILURE;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        auto parsed = starforge::bench::BenchmarkResult::fromJson(buffer.str());
        if (!parsed)
        {
            starforge::core::logError("budget", "invalid baseline JSON: {}", parsed.error());
            return EXIT_FAILURE;
        }
        baseline = std::move(*parsed);
    }

    const auto comparison = starforge::bench::compareBudgets(*budgets, baseline);
    auto written = starforge::bench::writeBudgetReport(comparison, reportPath);
    if (!written)
    {
        starforge::core::logError("budget", "{}", written.error());
        return EXIT_FAILURE;
    }

    std::cout << "Wrote " << reportPath << ": " << comparison.metCount() << " met, "
              << comparison.overCount() << " over, " << comparison.unknownCount() << " unknown\n";
    return EXIT_SUCCESS;
}
