#include "job_system_adapter.hpp"

namespace starforge::physics
{

void JobsAdapter::executeJob(void* userData)
{
    auto* job = static_cast<Job*>(userData);
    job->Execute();
    job->Release();
}

JobsAdapter::JobHandle JobsAdapter::CreateJob(
    const char* inName, JPH::ColorArg inColor, const JPH::JobSystem::JobFunction& inJobFunction,
    std::uint32_t inNumDependencies)
{
    auto* job = new Job(inName, inColor, this, inJobFunction, inNumDependencies);
    JobHandle handle(job);
    if (inNumDependencies == 0)
    {
        QueueJob(job);
    }
    return handle;
}

void JobsAdapter::QueueJob(Job* inJob)
{
    // Hold a reference while the job waits in our scheduler's queue;
    // executeJob releases it after running.
    inJob->AddRef();
    jobs::submit(executeJob, inJob);
}

void JobsAdapter::QueueJobs(Job** inJobs, std::uint32_t inNumJobs)
{
    for (std::uint32_t i = 0; i < inNumJobs; ++i)
    {
        QueueJob(inJobs[i]);
    }
}

void JobsAdapter::FreeJob(Job* inJob)
{
    // Jolt takes ownership of the job memory once Released for the last
    // time; the JobSystem deletes it here.
    delete inJob;
}

} // namespace starforge::physics
