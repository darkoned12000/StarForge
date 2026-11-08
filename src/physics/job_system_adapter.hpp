#pragma once

// Jolt JobSystem adapter onto starforge::jobs::JobScheduler, so physics
// scheduling shares the engine's existing worker pool instead of
// spawning a competing OS thread pool (ADR-004 frame budget).
// Barrier waiting and work-helping come from JobSystemWithBarrier.

#include "jobs/job_scheduler.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace starforge::physics
{

class JobsAdapter final : public JPH::JobSystemWithBarrier
{
public:
    explicit JobsAdapter(std::uint32_t maxBarriers) : JobSystemWithBarrier(maxBarriers)
    {
    }

    ~JobsAdapter() override = default;

    int GetMaxConcurrency() const override
    {
        return static_cast<int>(jobs::globalScheduler().threadCount()) + 1;
    }

    JobHandle CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction,
                        std::uint32_t inNumDependencies = 0) override;

private:
    static void executeJob(void* userData);

protected:
    void QueueJob(Job* inJob) override;
    void QueueJobs(Job** inJobs, std::uint32_t inNumJobs) override;
    void FreeJob(Job* inJob) override;
};

} // namespace starforge::physics
