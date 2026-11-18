#include <doctest/doctest.h>

#include "jobs/job_scheduler.hpp"
#include "profiling/profile.hpp"

#if defined(STARFORGE_ENABLE_PROFILING) && STARFORGE_ENABLE_PROFILING
#define SF_TEST_PROFILING_ENABLED 1
#else
#define SF_TEST_PROFILING_ENABLED 0
#endif

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

using namespace starforge::profiling;
using namespace starforge::jobs;

namespace
{

std::size_t countKind(const std::vector<Event>& events, EventKind kind)
{
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
                                                   [kind](const Event& e) { return e.kind == kind; }));
}

std::size_t countNamed(const std::vector<Event>& events, EventKind kind, const char* name)
{
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.kind == kind && e.name != nullptr && std::string_view(e.name) == name;
    }));
}

} // namespace

TEST_CASE("Profiling: capture is opt-in and off by default")
{
    enableCapture(false);
    clearCapture();

    beginZone("test::ignored");
    endZone();

    CHECK(drainCapture().empty());
}

#if SF_TEST_PROFILING_ENABLED
TEST_CASE("Profiling: zone scope records begin/end and per-thread CPU time")
{
    enableCapture(true);
    resetThreadStats();

    {
        SF_PROFILE_ZONE("test::work");
        volatile std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < 100000; ++i) sink += i;
        (void)sink;
    }

    const auto events = drainCapture();
    CHECK(countNamed(events, EventKind::ZoneBegin, "test::work") == 1);
    CHECK(countKind(events, EventKind::ZoneEnd) == 1);
    CHECK(threadZoneTimeNs() > 0);

    enableCapture(false);
}

TEST_CASE("Profiling: thread names and logical fiber zones are captured")
{
    enableCapture(true);

    SF_PROFILE_THREAD("test::thread");
    SF_PROFILE_FIBER_ENTER("test::fiber");
    SF_PROFILE_FIBER_EXIT();

    const auto events = drainCapture();
    CHECK(countNamed(events, EventKind::ThreadName, "test::thread") == 1);
    CHECK(countNamed(events, EventKind::FiberEnter, "test::fiber") == 1);
    CHECK(countKind(events, EventKind::FiberExit) == 1);

    enableCapture(false);
}
#endif // SF_TEST_PROFILING_ENABLED

TEST_CASE("Profiling: drain clears the buffer and reset zeroes thread stats")
{
    enableCapture(true);
    resetThreadStats();

    beginZone("test::one");
    endZone();
    CHECK_FALSE(drainCapture().empty());
    CHECK(drainCapture().empty());

    resetThreadStats();
    CHECK(threadZoneTimeNs() == 0);

    enableCapture(false);
}

#if SF_TEST_PROFILING_ENABLED
TEST_CASE("Profiling: every job task emits a named zone and fiber scope")
{
    enableCapture(true);
    initGlobalScheduler(SchedulerConfig{.threadCount = 2});

    const auto noop = [](void*) {};
    std::vector<TaskHandle> handles;
    for (int i = 0; i < 8; ++i) handles.push_back(submit(noop));
    globalScheduler().waitAll();
    shutdownGlobalScheduler();

    const auto events = drainCapture();
    CHECK(countNamed(events, EventKind::ZoneBegin, "job::execute") == 8);
    CHECK(countNamed(events, EventKind::FiberEnter, "job::task") == 8);
    CHECK(countKind(events, EventKind::FiberExit) == 8);
    CHECK(countKind(events, EventKind::ThreadName) >= 1); // workers name themselves at startup

    enableCapture(false);
}

TEST_CASE("Profiling: work-helping tasks are instrumented too")
{
    enableCapture(true);
    initGlobalScheduler(SchedulerConfig{.threadCount = 1, .mode = SchedulerMode::SingleThreaded});

    const auto noop = [](void*) {};
    std::vector<TaskHandle> handles;
    for (int i = 0; i < 3; ++i) handles.push_back(submit(noop));
    globalScheduler().waitAll();
    shutdownGlobalScheduler();

    const auto events = drainCapture();
    CHECK(countNamed(events, EventKind::ZoneBegin, "job::execute") == 3);

    enableCapture(false);
}

TEST_CASE("Profiling: events from multiple threads are collected globally")
{
    enableCapture(true);
    clearCapture();

    std::thread a([] { SF_PROFILE_ZONE("test::thread_a"); });
    std::thread b([] { SF_PROFILE_ZONE("test::thread_b"); });
    a.join();
    b.join();

    const auto events = drainCapture();
    CHECK(countNamed(events, EventKind::ZoneBegin, "test::thread_a") == 1);
    CHECK(countNamed(events, EventKind::ZoneBegin, "test::thread_b") == 1);

    enableCapture(false);
}
#else
TEST_CASE("Profiling: instrumentation compiles out when disabled")
{
    enableCapture(true);
    SF_PROFILE_ZONE("test::compiled_out");
    SF_PROFILE_FIBER_ENTER("test::compiled_out");
    SF_PROFILE_FIBER_EXIT();
    CHECK(drainCapture().empty()); // macros are no-ops; direct API still works
    enableCapture(false);
}
#endif // SF_TEST_PROFILING_ENABLED
