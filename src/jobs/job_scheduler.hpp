#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace starforge::jobs
{

enum class SchedulerMode
{
    Normal,        // Work-stealing enabled
    Deterministic, // No stealing, seeded yields
    SingleThreaded // No worker threads (debugging)
};

struct TaskHandle
{
    std::uint64_t id = 0;
    bool isValid() const noexcept { return id != 0; }
    bool operator==(const TaskHandle& o) const noexcept { return id == o.id; }
};

using TaskFn = void(*)(void* userData);

struct SchedulerConfig
{
    std::uint32_t threadCount = 0; // 0 = hardware_concurrency() - 1
    SchedulerMode mode = SchedulerMode::Normal;
    std::uint32_t dequeCapacity = 4096; // power of two
};

class JobScheduler
{
public:
    explicit JobScheduler(const SchedulerConfig& config = {});
    ~JobScheduler();

    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    TaskHandle submit(TaskFn fn, void* userData = nullptr);
    TaskHandle submit(TaskFn fn, void* userData, std::span<const TaskHandle> dependencies);

    void wait(TaskHandle handle);
    void waitAll();

    std::uint32_t threadCount() const noexcept;
    bool isDeterministic() const noexcept;

    void* scratchAlloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    void resetScratch();

    // Deferred deletion: retire a pointer for deletion after N epochs.
    void retire(void* ptr, void (*deleter)(void*));
    void collectRetired(std::uint32_t currentEpoch);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

JobScheduler& globalScheduler();
void initGlobalScheduler(const SchedulerConfig& config = {});
void shutdownGlobalScheduler();

inline TaskHandle submit(TaskFn fn, void* userData = nullptr)
{
    return globalScheduler().submit(fn, userData);
}

} // namespace starforge::jobs
