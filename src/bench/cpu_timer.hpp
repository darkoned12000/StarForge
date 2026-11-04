#pragma once

#include "stats/statistics.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace starforge::bench
{

// High-resolution CPU timer for benchmark measurement phases.
// Uses std::chrono::steady_clock for monotonic wall-clock timing.
class CpuTimer
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double, std::milli>;

    CpuTimer() = default;

    // Mark the start of a frame (overall frame timing).
    void beginFrame() noexcept;

    // Mark the start of command recording phase.
    void beginCommandRecording() noexcept;

    // Mark the end of command recording phase.
    void endCommandRecording() noexcept;

    // Mark the start of submission phase.
    void beginSubmission() noexcept;

    // Mark the end of submission phase.
    void endSubmission() noexcept;

    // Mark the end of the frame. Computes totalFrameMs.
    void endFrame() noexcept;

    // Results for the current frame (in milliseconds).
    [[nodiscard]] double framePreparationMs() const noexcept;
    [[nodiscard]] double commandRecordingMs() const noexcept;
    [[nodiscard]] double submissionMs() const noexcept;
    [[nodiscard]] double totalFrameMs() const noexcept;

    // Reset all timing state for the next frame.
    void reset() noexcept;

private:
    TimePoint frameStart_{};
    TimePoint frameEnd_{};
    TimePoint commandRecordingStart_{};
    TimePoint submissionStart_{};

    double commandRecordingMs_ = 0.0;
    double submissionMs_ = 0.0;
    bool frameStarted_ = false;
    bool commandRecordingActive_ = false;
    bool submissionActive_ = false;
};

// Accumulates per-frame timing samples for statistical analysis.
struct FrameTimingSample
{
    double framePreparationMs = 0.0;
    double commandRecordingMs = 0.0;
    double submissionMs = 0.0;
    double totalFrameMs = 0.0;
};

class FrameTimingCollector
{
public:
    FrameTimingCollector() = default;

    void addSample(const FrameTimingSample& sample);

    [[nodiscard]] std::size_t count() const noexcept { return samples_.size(); }
    void clear() noexcept { samples_.clear(); }

    [[nodiscard]] std::vector<double> framePreparationSamples() const;
    [[nodiscard]] std::vector<double> commandRecordingSamples() const;
    [[nodiscard]] std::vector<double> submissionSamples() const;
    [[nodiscard]] std::vector<double> totalFrameSamples() const;

    [[nodiscard]] stats::Statistics framePreparationStats() const;
    [[nodiscard]] stats::Statistics commandRecordingStats() const;
    [[nodiscard]] stats::Statistics submissionStats() const;
    [[nodiscard]] stats::Statistics totalFrameStats() const;

private:
    std::vector<FrameTimingSample> samples_;
};

} // namespace starforge::bench
