#include "profiling/profile.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

namespace starforge::profiling
{

namespace
{

std::atomic<bool> g_capture{false};
std::mutex g_eventsMutex;
std::vector<Event> g_events;

struct ThreadStats
{
    std::uint64_t zoneTimeNs = 0;
    std::uint32_t depth = 0;
    std::chrono::steady_clock::time_point zoneStart{};
};
thread_local ThreadStats t_stats;

void record(EventKind kind, const char* name) noexcept
{
    if (!g_capture.load(std::memory_order_relaxed))
    {
        return;
    }

    try
    {
        std::lock_guard lock(g_eventsMutex);
        if (!g_capture.load(std::memory_order_relaxed))
        {
            return;
        }

        g_events.push_back(Event{kind, name ? std::string{name} : std::string{}});
    }
    catch (...)
    {
        // Profiling capture is best-effort and must never break callers.
    }
}

} // namespace

void enableCapture(bool enabled) noexcept
{
    std::lock_guard lock(g_eventsMutex);
    g_capture.store(enabled, std::memory_order_relaxed);
    if (enabled)
    {
        g_events.clear();
    }
}

bool captureEnabled() noexcept
{
    return g_capture.load(std::memory_order_relaxed);
}

std::vector<Event> drainCapture()
{
    std::lock_guard lock(g_eventsMutex);
    std::vector<Event> result;
    result.swap(g_events);
    return result;
}

void clearCapture()
{
    std::lock_guard lock(g_eventsMutex);
    g_events.clear();
}

std::uint64_t threadZoneTimeNs() noexcept
{
    return t_stats.zoneTimeNs;
}

void resetThreadStats() noexcept
{
    t_stats.zoneTimeNs = 0;
    t_stats.depth = 0;
    t_stats.zoneStart = {};
}

void setThreadName(const char* name) noexcept
{
    record(EventKind::ThreadName, name);
}

void beginZone(const char* name) noexcept
{
    record(EventKind::ZoneBegin, name);
    if (t_stats.depth == 0)
    {
        t_stats.zoneStart = std::chrono::steady_clock::now();
    }
    ++t_stats.depth;
}

void endZone() noexcept
{
    const auto end = std::chrono::steady_clock::now();
    if (t_stats.depth == 0)
    {
        record(EventKind::ZoneEnd, {});
        return;
    }

    --t_stats.depth;
    if (t_stats.depth == 0)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - t_stats.zoneStart);
        if (elapsed.count() > 0)
        {
            t_stats.zoneTimeNs += static_cast<std::uint64_t>(elapsed.count());
        }
        t_stats.zoneStart = {};
    }

    record(EventKind::ZoneEnd, {});
}

void fiberEnter(const char* name) noexcept
{
    record(EventKind::FiberEnter, name);
}

void fiberExit() noexcept
{
    record(EventKind::FiberExit, nullptr);
}

} // namespace starforge::profiling
