#include "bench/cpu_timer.hpp"

#include "stats/statistics.hpp"

#include <chrono>

namespace starforge::bench
{

void CpuTimer::beginFrame() noexcept
{
    frameStart_ = Clock::now();
    frameStarted_ = true;
    commandRecordingMs_ = 0.0;
    submissionMs_ = 0.0;
    commandRecordingActive_ = false;
    submissionActive_ = false;
}

void CpuTimer::beginCommandRecording() noexcept
{
    commandRecordingStart_ = Clock::now();
    commandRecordingActive_ = true;
}

void CpuTimer::endCommandRecording() noexcept
{
    if (commandRecordingActive_)
    {
        auto end = Clock::now();
        commandRecordingMs_ += Duration(end - commandRecordingStart_).count();
        commandRecordingActive_ = false;
    }
}

void CpuTimer::beginSubmission() noexcept
{
    submissionStart_ = Clock::now();
    submissionActive_ = true;
}

void CpuTimer::endSubmission() noexcept
{
    if (submissionActive_)
    {
        auto end = Clock::now();
        submissionMs_ += Duration(end - submissionStart_).count();
        submissionActive_ = false;
    }
}

void CpuTimer::endFrame() noexcept
{
    if (frameStarted_)
    {
        frameEnd_ = Clock::now();
        frameStarted_ = false;
    }
}

double CpuTimer::framePreparationMs() const noexcept
{
    // Frame preparation = total - command recording - submission.
    // This is an approximation since phases may overlap.
    double total = totalFrameMs();
    double prep = total - commandRecordingMs_ - submissionMs_;
    return (prep > 0.0) ? prep : 0.0;
}

double CpuTimer::commandRecordingMs() const noexcept
{
    return commandRecordingMs_;
}

double CpuTimer::submissionMs() const noexcept
{
    return submissionMs_;
}

double CpuTimer::totalFrameMs() const noexcept
{
    if (frameEnd_ > frameStart_)
    {
        return Duration(frameEnd_ - frameStart_).count();
    }
    return commandRecordingMs_ + submissionMs_;
}

void CpuTimer::reset() noexcept
{
    frameStarted_ = false;
    commandRecordingActive_ = false;
    submissionActive_ = false;
    commandRecordingMs_ = 0.0;
    submissionMs_ = 0.0;
    frameStart_ = TimePoint{};
    frameEnd_ = TimePoint{};
}

// FrameTimingCollector implementation.

void FrameTimingCollector::addSample(const FrameTimingSample& sample)
{
    samples_.push_back(sample);
}

std::vector<double> FrameTimingCollector::framePreparationSamples() const
{
    std::vector<double> result;
    result.reserve(samples_.size());
    for (const auto& s : samples_)
    {
        result.push_back(s.framePreparationMs);
    }
    return result;
}

std::vector<double> FrameTimingCollector::commandRecordingSamples() const
{
    std::vector<double> result;
    result.reserve(samples_.size());
    for (const auto& s : samples_)
    {
        result.push_back(s.commandRecordingMs);
    }
    return result;
}

std::vector<double> FrameTimingCollector::submissionSamples() const
{
    std::vector<double> result;
    result.reserve(samples_.size());
    for (const auto& s : samples_)
    {
        result.push_back(s.submissionMs);
    }
    return result;
}

std::vector<double> FrameTimingCollector::totalFrameSamples() const
{
    std::vector<double> result;
    result.reserve(samples_.size());
    for (const auto& s : samples_)
    {
        result.push_back(s.totalFrameMs);
    }
    return result;
}

stats::Statistics FrameTimingCollector::framePreparationStats() const
{
    auto samples = framePreparationSamples();
    return stats::compute(samples);
}

stats::Statistics FrameTimingCollector::commandRecordingStats() const
{
    auto samples = commandRecordingSamples();
    return stats::compute(samples);
}

stats::Statistics FrameTimingCollector::submissionStats() const
{
    auto samples = submissionSamples();
    return stats::compute(samples);
}

stats::Statistics FrameTimingCollector::totalFrameStats() const
{
    auto samples = totalFrameSamples();
    return stats::compute(samples);
}

} // namespace starforge::bench
