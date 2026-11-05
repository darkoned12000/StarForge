#include <doctest/doctest.h>

#include "content/content_generation.hpp"
#include "content/cell_traversal.hpp"
#include "jobs/job_scheduler.hpp"
#include "planets/terrain.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace starforge::content;
using namespace starforge::jobs;

TEST_CASE("Determinism: content generation identical across thread counts")
{
    const std::uint64_t seed = 42;
    starforge::spatial::WorldPosition cam;
    cam.cellX = 0; cam.cellY = 0; cam.cellZ = 0;
    cam.offsetX = 0; cam.offsetY = 0; cam.offsetZ = 0;

    auto cells = traverseCells(cam, 0, 2, 1000000.0);

    // Run with 1 thread
    initGlobalScheduler(SchedulerConfig{.threadCount = 1});
    auto result1 = generateContentParallel(seed, cells);
    shutdownGlobalScheduler();

    // Run with 4 threads
    initGlobalScheduler(SchedulerConfig{.threadCount = 4});
    auto result4 = generateContentParallel(seed, cells);
    shutdownGlobalScheduler();

    // Run with 8 threads
    initGlobalScheduler(SchedulerConfig{.threadCount = 8});
    auto result8 = generateContentParallel(seed, cells);
    shutdownGlobalScheduler();

    // Verify identical
    CHECK(result1.size() == result4.size());
    CHECK(result1.size() == result8.size());

    for (std::size_t i = 0; i < result1.size(); ++i)
    {
        CHECK(result1[i].objects.size() == result4[i].objects.size());
        CHECK(result1[i].objects.size() == result8[i].objects.size());
        for (std::size_t j = 0; j < result1[i].objects.size(); ++j)
        {
            CHECK(result1[i].objects[j].objectId == result4[i].objects[j].objectId);
            CHECK(result1[i].objects[j].localX == result4[i].objects[j].localX);
            CHECK(result1[i].objects[j].localY == result4[i].objects[j].localY);
            CHECK(result1[i].objects[j].localZ == result4[i].objects[j].localZ);
            CHECK(result1[i].objects[j].scale == result4[i].objects[j].scale);
        }
    }
}

TEST_CASE("Determinism: terrain height identical across thread counts")
{
    starforge::planets::TerrainHeightGenerator gen(12345);
    std::vector<starforge::math::Vec3d> dirs;
    for (int i = 0; i < 1000; ++i)
    {
        double theta = static_cast<double>(i) * 0.01;
        double phi = static_cast<double>(i) * 0.02;
        dirs.push_back({std::sin(theta) * std::cos(phi),
                       std::sin(theta) * std::sin(phi),
                       std::cos(theta)});
    }

    // 1 thread
    initGlobalScheduler(SchedulerConfig{.threadCount = 1});
    auto r1 = gen.heightFractalBatch(dirs);
    shutdownGlobalScheduler();

    // 4 threads
    initGlobalScheduler(SchedulerConfig{.threadCount = 4});
    auto r4 = gen.heightFractalBatch(dirs);
    shutdownGlobalScheduler();

    // 8 threads
    initGlobalScheduler(SchedulerConfig{.threadCount = 8});
    auto r8 = gen.heightFractalBatch(dirs);
    shutdownGlobalScheduler();

    CHECK(r1.size() == r4.size());
    CHECK(r1.size() == r8.size());
    for (std::size_t i = 0; i < r1.size(); ++i)
    {
        CHECK(r1[i] == r4[i]);
        CHECK(r1[i] == r8[i]);
    }
}

TEST_CASE("Stress: high concurrency task execution")
{
    initGlobalScheduler(SchedulerConfig{.threadCount = 8});
    std::atomic<int> counter{0};
    auto task = [](void* p) {
        auto* c = static_cast<std::atomic<int>*>(p);
        // Random small work
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) x += i;
        c->fetch_add(1);
    };

    std::vector<TaskHandle> handles;
    handles.reserve(1000);
    for (int i = 0; i < 1000; ++i)
    {
        handles.push_back(submit(task, &counter));
    }

    globalScheduler().waitAll();
    CHECK(counter.load() == 1000);
    shutdownGlobalScheduler();
}