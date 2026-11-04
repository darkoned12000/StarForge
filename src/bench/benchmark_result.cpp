#include "bench/benchmark_result.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace starforge::bench
{

using json = nlohmann::json;

namespace
{

template <typename T>
T valueOr(const json& j, const std::string& key, const T& fallback)
{
    if (j.contains(key) && !j[key].is_null())
    {
        return j[key].get<T>();
    }
    return fallback;
}

json statsToJson(const stats::Statistics& s)
{
    json j;
    j["min"] = s.min;
    j["max"] = s.max;
    j["mean"] = s.mean;
    j["median"] = s.median;
    j["p95"] = s.p95;
    j["p99"] = s.p99;
    j["stddev"] = s.stddev;
    j["count"] = s.count;
    j["valid"] = s.valid;
    return j;
}

stats::Statistics statsFromJson(const json& j)
{
    stats::Statistics s;
    s.min = valueOr<double>(j, "min", 0.0);
    s.max = valueOr<double>(j, "max", 0.0);
    s.mean = valueOr<double>(j, "mean", 0.0);
    s.median = valueOr<double>(j, "median", 0.0);
    s.p95 = valueOr<double>(j, "p95", 0.0);
    s.p99 = valueOr<double>(j, "p99", 0.0);
    s.stddev = valueOr<double>(j, "stddev", 0.0);
    s.count = valueOr<std::size_t>(j, "count", 0);
    s.valid = valueOr<bool>(j, "valid", false);
    return s;
}

json metricToJson(const Metric& m)
{
    json j;
    j["summary"] = statsToJson(m.summary);
    j["raw_samples"] = m.rawSamples;
    return j;
}

Metric metricFromJson(const json& j)
{
    Metric m;
    if (j.contains("summary"))
    {
        m.summary = statsFromJson(j["summary"]);
    }
    if (j.contains("raw_samples") && j["raw_samples"].is_array())
    {
        for (const auto& v : j["raw_samples"])
        {
            m.rawSamples.push_back(v.get<double>());
        }
    }
    return m;
}

json optionalDoubleToJson(const std::optional<double>& v)
{
    if (v.has_value())
    {
        return v.value();
    }
    return nullptr;
}

std::optional<double> optionalDoubleFromJson(const json& j, const std::string& key)
{
    if (j.contains(key) && !j[key].is_null())
    {
        return j[key].get<double>();
    }
    return std::nullopt;
}

} // namespace

std::string BenchmarkResult::toJson() const
{
    json j;

    j["schema_version"] = schemaVersion;
    j["benchmark_id"] = benchmarkId;
    j["workload_id"] = workloadId;
    j["timing_mode"] = timingMode;

    // Source.
    j["source"]["commit_hash"] = source.commitHash;
    j["source"]["dirty"] = source.dirty;
    if (source.diffHash.has_value())
    {
        j["source"]["diff_hash"] = source.diffHash.value();
    }
    else
    {
        j["source"]["diff_hash"] = nullptr;
    }

    // GPU.
    j["gpu"]["device_name"] = gpu.deviceName;
    j["gpu"]["vendor_id"] = gpu.vendorId;
    j["gpu"]["driver_version"] = gpu.driverVersion;
    j["gpu"]["vulkan_api_version"] = gpu.vulkanApiVersion;
    j["gpu"]["timestamp_valid_bits"] = gpu.timestampValidBits;
    j["gpu"]["timestamp_period_ns"] = gpu.timestampPeriodNs;

    // CPU.
    j["cpu"]["model"] = cpu.model;
    j["cpu"]["core_count"] = cpu.coreCount;
    j["cpu"]["thread_count"] = cpu.threadCount;
    j["cpu"]["governor"] = cpu.governor;

    // System.
    j["system"]["os"] = system.os;
    j["system"]["kernel"] = system.kernel;
    j["system"]["ram_bytes"] = system.ramBytes;

    // Build.
    j["build"]["compiler"] = build.compiler;
    j["build"]["build_type"] = build.buildType;
    j["build"]["flags"] = build.flags;
    j["build"]["validation_enabled"] = build.validationEnabled;

    // Workload.
    j["workload"]["scene_id"] = workload.sceneId;
    j["workload"]["seed"] = workload.seed;
    j["workload"]["instance_count"] = workload.instanceCount;
    j["workload"]["visible_fraction"] = workload.visibleFraction;
    j["workload"]["resolution_width"] = workload.resolutionWidth;
    j["workload"]["resolution_height"] = workload.resolutionHeight;
    j["workload"]["mesh_complexity"] = workload.meshComplexity;
    j["workload"]["material_count"] = workload.materialCount;

    // Environment.
    j["environment"]["gpu_clock_mhz"] = optionalDoubleToJson(environment.gpuClockMhz);
    j["environment"]["gpu_temp_c"] = optionalDoubleToJson(environment.gpuTempC);
    j["environment"]["gpu_power_w"] = optionalDoubleToJson(environment.gpuPowerW);
    j["environment"]["cpu_freq_mhz"] = optionalDoubleToJson(environment.cpuFreqMhz);
    j["environment"]["cpu_temp_c"] = optionalDoubleToJson(environment.cpuTempC);

    // Metrics.
    for (const auto& [name, metric] : metrics)
    {
        j["metrics"][name] = metricToJson(metric);
    }

    // Memory.
    j["memory"]["allocated_bytes"] = memory.allocatedBytes;
    j["memory"]["used_bytes"] = memory.usedBytes;
    j["memory"]["allocation_count"] = memory.allocationCount;
    j["memory"]["block_count"] = memory.blockCount;

    return j.dump(2);
}

std::expected<BenchmarkResult, std::string> BenchmarkResult::fromJson(const std::string& jsonStr)
{
    json j;
    try
    {
        j = json::parse(jsonStr);
    }
    catch (const json::parse_error& e)
    {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }

    try
    {
    BenchmarkResult result;

    result.schemaVersion = j.value("schema_version", kSchemaVersion);
    result.benchmarkId = j.value("benchmark_id", "");
    result.workloadId = j.value("workload_id", "");
    result.timingMode = j.value("timing_mode", "");

    // Source.
    if (j.contains("source"))
    {
        const auto& s = j["source"];
        result.source.commitHash = s.value("commit_hash", "");
        result.source.dirty = s.value("dirty", false);
        if (s.contains("diff_hash") && !s["diff_hash"].is_null())
        {
            result.source.diffHash = s["diff_hash"].get<std::string>();
        }
    }

    // GPU.
    if (j.contains("gpu"))
    {
        const auto& g = j["gpu"];
        result.gpu.deviceName = g.value("device_name", "");
        result.gpu.vendorId = g.value("vendor_id", 0u);
        result.gpu.driverVersion = g.value("driver_version", "");
        result.gpu.vulkanApiVersion = g.value("vulkan_api_version", "");
        result.gpu.timestampValidBits = g.value("timestamp_valid_bits", 0u);
        result.gpu.timestampPeriodNs = g.value("timestamp_period_ns", 0.0);
    }

    // CPU.
    if (j.contains("cpu"))
    {
        const auto& c = j["cpu"];
        result.cpu.model = c.value("model", "");
        result.cpu.coreCount = c.value("core_count", 0u);
        result.cpu.threadCount = c.value("thread_count", 0u);
        result.cpu.governor = c.value("governor", "");
    }

    // System.
    if (j.contains("system"))
    {
        const auto& s = j["system"];
        result.system.os = s.value("os", "");
        result.system.kernel = s.value("kernel", "");
        result.system.ramBytes = s.value("ram_bytes", 0ull);
    }

    // Build.
    if (j.contains("build"))
    {
        const auto& b = j["build"];
        result.build.compiler = b.value("compiler", "");
        result.build.buildType = b.value("build_type", "");
        result.build.flags = b.value("flags", "");
        result.build.validationEnabled = b.value("validation_enabled", false);
    }

    // Workload.
    if (j.contains("workload"))
    {
        const auto& wl = j["workload"];
        result.workload.sceneId = wl.value("scene_id", "");
        result.workload.seed = wl.value("seed", 0ull);
        result.workload.instanceCount = wl.value("instance_count", 0ull);
        result.workload.visibleFraction = wl.value("visible_fraction", 1.0);
        result.workload.resolutionWidth = wl.value("resolution_width", 0u);
        result.workload.resolutionHeight = wl.value("resolution_height", 0u);
        result.workload.meshComplexity = wl.value("mesh_complexity", "");
        result.workload.materialCount = wl.value("material_count", 0u);
    }

    // Environment.
    if (j.contains("environment"))
    {
        const auto& e = j["environment"];
        result.environment.gpuClockMhz = optionalDoubleFromJson(e, "gpu_clock_mhz");
        result.environment.gpuTempC = optionalDoubleFromJson(e, "gpu_temp_c");
        result.environment.gpuPowerW = optionalDoubleFromJson(e, "gpu_power_w");
        result.environment.cpuFreqMhz = optionalDoubleFromJson(e, "cpu_freq_mhz");
        result.environment.cpuTempC = optionalDoubleFromJson(e, "cpu_temp_c");
    }

    // Metrics.
    if (j.contains("metrics") && j["metrics"].is_object())
    {
        for (auto it = j["metrics"].begin(); it != j["metrics"].end(); ++it)
        {
            result.metrics[it.key()] = metricFromJson(it.value());
        }
    }

    // Memory.
    if (j.contains("memory"))
    {
        const auto& m = j["memory"];
        result.memory.allocatedBytes = m.value("allocated_bytes", 0ull);
        result.memory.usedBytes = m.value("used_bytes", 0ull);
        result.memory.allocationCount = m.value("allocation_count", 0ull);
        result.memory.blockCount = m.value("block_count", 0ull);
    }

    return result;
    }
    catch (const json::exception& e)
    {
        return std::unexpected(std::string("JSON deserialization error: ") + e.what());
    }
}

} // namespace starforge::bench
