#include "bench/thread_scaling.hpp"

#include "content/content_generation.hpp"
#include "jobs/job_scheduler.hpp"
#include "math/math.hpp"
#include "planets/terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <cstdint>
#include <vector>

namespace starforge::bench
{

using json = nlohmann::json;

namespace
{

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashBytes(std::uint64_t& hash, const void* data, std::size_t bytes)
{
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        hash ^= p[i];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(std::uint64_t& hash, const T& value)
{
    hashBytes(hash, &value, sizeof(T));
}

std::uint64_t workloadChecksum(const std::vector<content::ContentChunk>& chunks,
                               const std::vector<double>& heights)
{
    std::uint64_t hash = kFnvOffset;
    for (const auto& chunk : chunks)
    {
        hashValue(hash, chunk.cellX);
        hashValue(hash, chunk.cellY);
        hashValue(hash, chunk.cellZ);
        hashValue(hash, chunk.level);
        hashValue(hash, chunk.seed);
        hashValue(hash, chunk.objects.size());
        for (const auto& object : chunk.objects)
        {
            hashValue(hash, object.objectId);
            hashValue(hash, std::bit_cast<std::uint64_t>(object.localX));
            hashValue(hash, std::bit_cast<std::uint64_t>(object.localY));
            hashValue(hash, std::bit_cast<std::uint64_t>(object.localZ));
            hashValue(hash, std::bit_cast<std::uint32_t>(object.scale));
            hashValue(hash, object.subtype);
            hashValue(hash, object.properties);
            hashBytes(hash, object.orientation, sizeof(object.orientation));
        }
    }
    hashValue(hash, heights.size());
    for (double height : heights)
    {
        hashValue(hash, std::bit_cast<std::uint64_t>(height));
    }
    return hash;
}

std::vector<content::CellCoord> makeCells(const ThreadScalingConfig& config)
{
    std::vector<content::CellCoord> cells;
    cells.reserve(config.batchCount * config.cellsPerBatch);
    for (std::size_t b = 0; b < config.batchCount; ++b)
    {
        for (std::size_t c = 0; c < config.cellsPerBatch; ++c)
        {
            const std::int64_t index = static_cast<std::int64_t>(b * config.cellsPerBatch + c);
            cells.push_back(content::CellCoord{index, 0, 0, config.level});
        }
    }
    return cells;
}

std::vector<starforge::math::Vec3d> makeDirections(std::size_t count)
{
    std::vector<starforge::math::Vec3d> dirs;
    dirs.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) * 0.01;
        const double p = static_cast<double>(i) * 0.02;
        dirs.push_back({std::sin(t) * std::cos(p), std::sin(t) * std::sin(p), std::cos(t)});
    }
    return dirs;
}

double medianOf(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

// Read CPU metadata directly so the scaling tool stays independent of the
// Vulkan-backed benchmark harness (only the portable CPU facts are needed).
struct CpuFacts
{
    std::string model;
    std::uint32_t coreCount = 0;
};

CpuFacts readCpuFacts()
{
    CpuFacts facts;
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line))
    {
        if (line.find("model name") != std::string::npos)
        {
            const auto colon = line.find(':');
            if (colon != std::string::npos)
            {
                facts.model = line.substr(colon + 2);
                while (!facts.model.empty() && (facts.model.back() == '\n' || facts.model.back() == '\r'))
                {
                    facts.model.pop_back();
                }
            }
            break;
        }
    }
    std::ifstream cpuinfo2("/proc/cpuinfo");
    while (std::getline(cpuinfo2, line))
    {
        if (line.find("processor") == 0) ++facts.coreCount;
    }
    return facts;
}

class SchedulerGuard
{
public:
    explicit SchedulerGuard(std::uint32_t threads)
    {
        jobs::initGlobalScheduler(jobs::SchedulerConfig{.threadCount = threads});
    }
    ~SchedulerGuard() { jobs::shutdownGlobalScheduler(); }
    SchedulerGuard(const SchedulerGuard&) = delete;
    SchedulerGuard& operator=(const SchedulerGuard&) = delete;
};

} // namespace

std::expected<ThreadScalingResult, std::string>
runThreadScalingBenchmark(const ThreadScalingConfig& config)
{
    if (config.threadCounts.empty())
    {
        return std::unexpected("threadCounts must not be empty");
    }
    if (config.repeats == 0)
    {
        return std::unexpected("repeats must be at least 1");
    }
    if (config.batchCount == 0 || config.cellsPerBatch == 0 || config.heightSamples == 0)
    {
        return std::unexpected("workload sizes must be non-zero");
    }
    if (std::find(config.threadCounts.begin(), config.threadCounts.end(), 0) != config.threadCounts.end())
    {
        return std::unexpected("thread count 0 means hardware default, not a scaling sample");
    }

    ThreadScalingResult result;
    const auto cpu = readCpuFacts();
    result.cpuModel = cpu.model;
    result.cpuCoreCount = cpu.coreCount;
    result.generatedAt = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    result.config = config;

    const auto cells = makeCells(config);
    const auto directions = makeDirections(config.heightSamples);
    const planets::TerrainHeightGenerator terrain(12345);

    std::uint64_t baselineChecksum = 0;
    double baselineMedianMs = 0.0;
    bool haveBaseline = false;

    for (std::uint32_t threads : config.threadCounts)
    {
        ThreadScalingSample sample;
        sample.threadCount = threads;

        std::vector<double> times;
        times.reserve(config.repeats);
        for (std::uint32_t repeat = 0; repeat < config.repeats; ++repeat)
        {
            const auto start = std::chrono::steady_clock::now();
            std::vector<content::ContentChunk> chunks;
            std::vector<double> heights;
            {
                SchedulerGuard guard(threads);
                chunks = content::generateContentParallel(config.seed, cells, config.batchCount);
                heights = terrain.heightFractalBatch(directions);
            }
            const auto end = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            sample.checksum = workloadChecksum(chunks, heights);
        }

        sample.medianMs = medianOf(times);
        sample.bestMs = *std::min_element(times.begin(), times.end());

        if (!haveBaseline)
        {
            baselineChecksum = sample.checksum;
            baselineMedianMs = sample.medianMs;
            haveBaseline = true;
            sample.speedup = 1.0;
        }
        else
        {
            sample.speedup = (sample.medianMs > 0.0) ? baselineMedianMs / sample.medianMs : 0.0;
        }
        sample.bitIdentical = (sample.checksum == baselineChecksum);
        result.samples.push_back(sample);
    }

    return result;
}

std::string ThreadScalingResult::toJson() const
{
    json j;
    j["schema_version"] = schemaVersion;
    j["cpu_model"] = cpuModel;
    j["cpu_core_count"] = cpuCoreCount;
    j["generated_at_epoch_s"] = generatedAt;
    j["workload"] = {
        {"seed", config.seed},
        {"batch_count", config.batchCount},
        {"cells_per_batch", config.cellsPerBatch},
        {"level", config.level},
        {"height_samples", config.heightSamples},
        {"repeats", config.repeats},
    };
    j["samples"] = json::array();
    for (const auto& sample : samples)
    {
        j["samples"].push_back({
            {"thread_count", sample.threadCount},
            {"median_ms", sample.medianMs},
            {"best_ms", sample.bestMs},
            {"speedup", sample.speedup},
            {"checksum", sample.checksum},
            {"bit_identical", sample.bitIdentical},
        });
    }
    return j.dump(2);
}

std::expected<ThreadScalingResult, std::string>
ThreadScalingResult::fromJson(const std::string& text)
{
    json j;
    try
    {
        j = json::parse(text);
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("invalid scaling JSON: ") + e.what());
    }

    ThreadScalingResult result;
    result.schemaVersion = j.value("schema_version", 0u);
    result.cpuModel = j.value("cpu_model", "");
    result.cpuCoreCount = j.value("cpu_core_count", 0u);
    result.generatedAt = j.value("generated_at_epoch_s", "");

    if (j.contains("workload"))
    {
        const auto& w = j["workload"];
        result.config.seed = w.value("seed", 42ULL);
        result.config.batchCount = w.value("batch_count", std::size_t{24});
        result.config.cellsPerBatch = w.value("cells_per_batch", std::size_t{32});
        result.config.level = w.value("level", 0);
        result.config.heightSamples = w.value("height_samples", std::size_t{20000});
        result.config.repeats = w.value("repeats", std::uint32_t{3});
    }
    if (j.contains("samples") && j["samples"].is_array())
    {
        for (const auto& s : j["samples"])
        {
            ThreadScalingSample sample;
            sample.threadCount = s.value("thread_count", 0u);
            sample.medianMs = s.value("median_ms", 0.0);
            sample.bestMs = s.value("best_ms", 0.0);
            sample.speedup = s.value("speedup", 1.0);
            sample.checksum = s.value("checksum", 0ULL);
            sample.bitIdentical = s.value("bit_identical", false);
            result.samples.push_back(sample);
        }
    }
    return result;
}

} // namespace starforge::bench
