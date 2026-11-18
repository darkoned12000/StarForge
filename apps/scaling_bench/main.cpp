// 5.5.6c: run the deterministic content + terrain workload across worker
// thread counts and write a versioned JSON result.
//
// Usage: starforge_scaling_bench [--output FILE] [--threads 1,2,4,8,16]
//                                [--repeats N] [--cells-per-batch N] [--batches N]

#include "bench/thread_scaling.hpp"

#include <core/log.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

std::vector<std::uint32_t> parseThreadCounts(const std::string& csv)
{
    std::vector<std::uint32_t> counts;
    std::size_t start = 0;
    while (start <= csv.size())
    {
        const auto comma = csv.find(',', start);
        const auto token = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!token.empty())
        {
            counts.push_back(static_cast<std::uint32_t>(std::stoul(token)));
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return counts;
}

} // namespace

int main(int argc, char** argv)
{
    starforge::bench::ThreadScalingConfig config;
    std::string output = "benchmarks/scaling/thread_scaling.json";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc)
        {
            output = argv[++i];
        }
        else if (arg == "--threads" && i + 1 < argc)
        {
            config.threadCounts = parseThreadCounts(argv[++i]);
        }
        else if (arg == "--repeats" && i + 1 < argc)
        {
            config.repeats = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--cells-per-batch" && i + 1 < argc)
        {
            config.cellsPerBatch = static_cast<std::size_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--batches" && i + 1 < argc)
        {
            config.batchCount = static_cast<std::size_t>(std::stoul(argv[++i]));
        }
        else
        {
            std::cerr << "unknown or incomplete argument: " << arg << "\n";
            return EXIT_FAILURE;
        }
    }

    auto result = starforge::bench::runThreadScalingBenchmark(config);
    if (!result)
    {
        starforge::core::logError("scaling", "{}", result.error());
        return EXIT_FAILURE;
    }

    std::ofstream out(output, std::ios::trunc);
    if (!out)
    {
        starforge::core::logError("scaling", "cannot write {}", output);
        return EXIT_FAILURE;
    }
    out << result->toJson();
    if (!out)
    {
        starforge::core::logError("scaling", "failed writing {}", output);
        return EXIT_FAILURE;
    }

    for (const auto& sample : result->samples)
    {
        std::cout << sample.threadCount << " threads: " << sample.medianMs << " ms, "
                  << sample.speedup << "x speedup, bit-identical="
                  << (sample.bitIdentical ? "yes" : "NO") << "\n";
    }
    std::cout << "Wrote " << output << "\n";
    return EXIT_SUCCESS;
}
