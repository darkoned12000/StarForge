#include <doctest/doctest.h>

#include "jobs/job_scheduler.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace starforge::jobs;

TEST_CASE("JobScheduler: basic submit and wait")
{
    JobScheduler scheduler(SchedulerConfig{.threadCount = 2});
    std::atomic<int> counter{0};
    auto task = [](void* p) {
        auto* c = static_cast<std::atomic<int>*>(p);
        c->fetch_add(1);
    };
    auto h = scheduler.submit(task, &counter);
    scheduler.wait(h);
    CHECK(counter.load() == 1);
}

TEST_CASE("JobScheduler: multiple tasks")
{
    JobScheduler scheduler(SchedulerConfig{.threadCount = 4});
    std::atomic<int> counter{0};
    auto task = [](void* p) {
        auto* c = static_cast<std::atomic<int>*>(p);
        c->fetch_add(1);
    };
    std::vector<TaskHandle> handles;
    for (int i = 0; i < 100; ++i)
    {
        handles.push_back(scheduler.submit(task, &counter));
    }
    scheduler.waitAll();
    CHECK(counter.load() == 100);
}

TEST_CASE("JobScheduler: task dependencies")
{
    JobScheduler scheduler(SchedulerConfig{.threadCount = 2});
    std::atomic<int> order{0};
    int firstVal = 0, secondVal = 0;
    auto first = [](void* p) {
        auto* o = static_cast<std::atomic<int>*>(p);
        o->store(1);
    };
    auto second = [](void* p) {
        // Should run after first
    };
    auto h1 = scheduler.submit(first, &order);
    std::vector<TaskHandle> deps = {h1};
    auto h2 = scheduler.submit(second, nullptr, deps);
    scheduler.wait(h2);
    CHECK(order.load() == 1);
}

TEST_CASE("JobScheduler: single-threaded mode")
{
    JobScheduler scheduler(SchedulerConfig{.threadCount = 1, .mode = SchedulerMode::SingleThreaded});
    std::atomic<int> counter{0};
    auto task = [](void* p) {
        auto* c = static_cast<std::atomic<int>*>(p);
        c->fetch_add(1);
    };
    auto h = scheduler.submit(task, &counter);
    scheduler.wait(h);
    CHECK(counter.load() == 1);
}

TEST_CASE("JobScheduler: global scheduler")
{
    initGlobalScheduler(SchedulerConfig{.threadCount = 2});
    std::atomic<int> counter{0};
    auto task = [](void* p) {
        auto* c = static_cast<std::atomic<int>*>(p);
        c->fetch_add(1);
    };
    auto h = submit(task, &counter);
    globalScheduler().wait(h);
    CHECK(counter.load() == 1);
    shutdownGlobalScheduler();
}
