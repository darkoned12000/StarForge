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
    std::chrono::steady_clock::time_point zoneStart{};
};
thread_local ThreadStats t_stats;

void record(EventKind kind, const char* name) noexcept
{
    if (g_capture.load(std::memory_order_relaxed))
    {
        std::lock_guard lock(g_eventsMutex);
        g_events.push_back(Event{kind, name});
    }
}

} // namespace

void enableCapture(bool enabled) noexcept
{
    g_capture.store(enabled, std::memory_order_relaxed);
    if (enabled) clearCapture();
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
    t_stats.zoneStart = {};
}

void setThreadName(const char* name) noexcept
{
    record(EventKind::ThreadName, name);
}

void beginZone(const char* name) noexcept
{
    record(EventKind::ZoneBegin, name);
    t_stats.zoneStart = std::chrono::steady_clock::now();
}

void endZone() noexcept
{
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - t_stats.zoneStart);
    if (elapsed.count() > 0)
    {
        t_stats.zoneTimeNs += static_cast<std::uint64_t>(elapsed.count());
    }
    record(EventKind::ZoneEnd, nullptr);
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
