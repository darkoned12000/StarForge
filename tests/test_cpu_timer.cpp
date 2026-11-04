#include <doctest/doctest.h>

#include "bench/cpu_timer.hpp"

#include <chrono>
#include <thread>

using namespace starforge::bench;

TEST_CASE("CpuTimer: basic phase timing")
{
    CpuTimer timer;

    timer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.endFrame();

    CHECK(timer.totalFrameMs() >= 9.0); // at least 9 ms
    CHECK(timer.totalFrameMs() < 50.0); // less than 50 ms
}

TEST_CASE("CpuTimer: command recording timing")
{
    CpuTimer timer;

    timer.beginFrame();
    timer.beginCommandRecording();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.endCommandRecording();
    timer.endFrame();

    CHECK(timer.commandRecordingMs() >= 4.0);
    CHECK(timer.commandRecordingMs() < 20.0);
}

TEST_CASE("CpuTimer: submission timing")
{
    CpuTimer timer;

    timer.beginFrame();
    timer.beginSubmission();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.endSubmission();
    timer.endFrame();

    CHECK(timer.submissionMs() >= 4.0);
    CHECK(timer.submissionMs() < 20.0);
}

TEST_CASE("CpuTimer: frame preparation is total minus phases")
{
    CpuTimer timer;

    timer.beginFrame();
    timer.beginCommandRecording();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.endCommandRecording();
    timer.beginSubmission();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.endSubmission();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.endFrame();

    double total = timer.totalFrameMs();
    double prep = timer.framePreparationMs();
    double cmd = timer.commandRecordingMs();
    double sub = timer.submissionMs();

    // Preparation should be approximately total - cmd - sub.
    CHECK(prep >= 0.0);
    CHECK(total >= cmd + sub - 1.0); // allow 1 ms tolerance
}

TEST_CASE("CpuTimer: reset clears state")
{
    CpuTimer timer;

    timer.beginFrame();
    timer.beginCommandRecording();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.endCommandRecording();
    timer.endFrame();

    CHECK(timer.commandRecordingMs() > 0.0);

    timer.reset();
    CHECK(timer.commandRecordingMs() == 0.0);
    CHECK(timer.submissionMs() == 0.0);
    CHECK(timer.totalFrameMs() == 0.0);
}

TEST_CASE("CpuTimer: inactive phase returns 0")
{
    CpuTimer timer;
    timer.beginFrame();
    timer.endFrame();

    // No command recording or submission was started.
    CHECK(timer.commandRecordingMs() == 0.0);
    CHECK(timer.submissionMs() == 0.0);
}

TEST_CASE("FrameTimingCollector: basic usage")
{
    FrameTimingCollector collector;

    FrameTimingSample sample1;
    sample1.framePreparationMs = 1.0;
    sample1.commandRecordingMs = 2.0;
    sample1.submissionMs = 0.5;
    sample1.totalFrameMs = 3.5;
    collector.addSample(sample1);

    FrameTimingSample sample2;
    sample2.framePreparationMs = 1.5;
    sample2.commandRecordingMs = 2.5;
    sample2.submissionMs = 0.6;
    sample2.totalFrameMs = 4.6;
    collector.addSample(sample2);

    CHECK(collector.count() == 2);

    auto prepSamples = collector.framePreparationSamples();
    CHECK(prepSamples.size() == 2);
    CHECK(prepSamples[0] == 1.0);
    CHECK(prepSamples[1] == 1.5);

    auto cmdSamples = collector.commandRecordingSamples();
    CHECK(cmdSamples.size() == 2);
    CHECK(cmdSamples[0] == 2.0);

    auto subSamples = collector.submissionSamples();
    CHECK(subSamples.size() == 2);

    auto totalSamples = collector.totalFrameSamples();
    CHECK(totalSamples.size() == 2);
    CHECK(totalSamples[1] == 4.6);

    // Statistics.
    auto prepStats = collector.framePreparationStats();
    CHECK(prepStats.valid);
    CHECK(prepStats.count == 2);
    CHECK(prepStats.mean == doctest::Approx(1.25));

    auto cmdStats = collector.commandRecordingStats();
    CHECK(cmdStats.valid);
    CHECK(cmdStats.mean == doctest::Approx(2.25));

    auto subStats = collector.submissionStats();
    CHECK(subStats.valid);

    auto totalStats = collector.totalFrameStats();
    CHECK(totalStats.valid);
    CHECK(totalStats.mean == doctest::Approx(4.05));
}

TEST_CASE("FrameTimingCollector: clear")
{
    FrameTimingCollector collector;
    FrameTimingSample sample;
    sample.totalFrameMs = 1.0;
    collector.addSample(sample);
    CHECK(collector.count() == 1);

    collector.clear();
    CHECK(collector.count() == 0);
}

TEST_CASE("FrameTimingCollector: empty stats")
{
    FrameTimingCollector collector;
    auto stats = collector.totalFrameStats();
    CHECK_FALSE(stats.valid);
    CHECK(stats.count == 0);
}
