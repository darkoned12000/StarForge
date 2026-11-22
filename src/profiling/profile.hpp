#pragma once

// 5.5.1e — project profiling wrapper.
//
// All instrumentation goes through the SF_PROFILE_* macros. With
// STARFORGE_ENABLE_PROFILING=OFF they expand to nothing, so profiling is
// compiled out entirely. With STARFORGE_ENABLE_TRACY=ON they also emit Tracy
// zones (thread + fiber + scoped zones); the built-in capture API always
// provides a dependency-free, testable view of the same events.
//
// Names passed to profiling APIs are copied when capture is enabled, so
// callers do not need to keep string storage alive after the call.

#include <cstdint>
#include <string>
#include <vector>

namespace starforge::profiling
{

enum class EventKind
{
    ZoneBegin,
    ZoneEnd,
    ThreadName,
    FiberEnter,
    FiberExit,
};

struct Event
{
    EventKind   kind = EventKind::ZoneBegin;
    std::string name;
};

// Enable/disable event capture (default off; zero-cost when off).
void enableCapture(bool enabled) noexcept;
[[nodiscard]] bool captureEnabled() noexcept;

// Take all captured events from every thread and clear the buffer.
[[nodiscard]] std::vector<Event> drainCapture();
void clearCapture();

// Per-thread CPU time spent inside profiler zones (nanoseconds).
[[nodiscard]] std::uint64_t threadZoneTimeNs() noexcept;
void resetThreadStats() noexcept;

// Instrumentation primitives. Prefer the macros below.
void setThreadName(const char* name) noexcept;
void beginZone(const char* name) noexcept;
void endZone() noexcept;
void fiberEnter(const char* name) noexcept;
void fiberExit() noexcept;

class ZoneScope
{
public:
    explicit ZoneScope(const char* name) noexcept { beginZone(name); }
    ~ZoneScope() { endZone(); }
    ZoneScope(const ZoneScope&) = delete;
    ZoneScope& operator=(const ZoneScope&) = delete;
};

} // namespace starforge::profiling

#if defined(STARFORGE_ENABLE_TRACY) && STARFORGE_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

#if defined(STARFORGE_ENABLE_PROFILING) && STARFORGE_ENABLE_PROFILING

#define SF_PROFILE_DETAIL_JOIN_IMPL(a, b) a##b
#define SF_PROFILE_DETAIL_JOIN(a, b) SF_PROFILE_DETAIL_JOIN_IMPL(a, b)

#if defined(STARFORGE_ENABLE_TRACY) && STARFORGE_ENABLE_TRACY
// Tracy zones are RAII objects, so they are emitted next to the capture scope.
// Use these macros at statement position (not as an unbraced if body).
#define SF_PROFILE_TRACY_ZONE(name) ZoneScopedN(name)
#define SF_PROFILE_TRACY_THREAD(name) ::tracy::SetThreadName(name)
#ifdef TracyFiberEnter
#define SF_PROFILE_TRACY_FIBER_ENTER(name) TracyFiberEnter(name)
#else
#define SF_PROFILE_TRACY_FIBER_ENTER(name) ((void)0)
#endif
#ifdef TracyFiberLeave
#define SF_PROFILE_TRACY_FIBER_EXIT() TracyFiberLeave
#else
#define SF_PROFILE_TRACY_FIBER_EXIT() ((void)0)
#endif
#else
#define SF_PROFILE_TRACY_ZONE(name) ((void)0)
#define SF_PROFILE_TRACY_THREAD(name) ((void)0)
#define SF_PROFILE_TRACY_FIBER_ENTER(name) ((void)0)
#define SF_PROFILE_TRACY_FIBER_EXIT() ((void)0)
#endif

#define SF_PROFILE_THREAD(name)     \
    ::starforge::profiling::setThreadName(name); \
    SF_PROFILE_TRACY_THREAD(name)
#define SF_PROFILE_ZONE(name)                                                                   \
    ::starforge::profiling::ZoneScope SF_PROFILE_DETAIL_JOIN(_sf_profile_zone_, __LINE__){name}; \
    SF_PROFILE_TRACY_ZONE(name)
#define SF_PROFILE_FIBER_ENTER(name) \
    ::starforge::profiling::fiberEnter(name); \
    SF_PROFILE_TRACY_FIBER_ENTER(name)
#define SF_PROFILE_FIBER_EXIT()      \
    ::starforge::profiling::fiberExit(); \
    SF_PROFILE_TRACY_FIBER_EXIT()

#else

#define SF_PROFILE_THREAD(name)  ((void)0)
#define SF_PROFILE_ZONE(name)    ((void)0)
#define SF_PROFILE_FIBER_ENTER(name) ((void)0)
#define SF_PROFILE_FIBER_EXIT()  ((void)0)

#endif
