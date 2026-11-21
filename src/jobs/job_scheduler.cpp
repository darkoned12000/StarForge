#include "jobs/job_scheduler.hpp"

#include "core/log.hpp"
#include "profiling/profile.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace starforge::jobs
{

namespace
{

struct Task
{
    std::uint64_t id = 0;
    TaskFn fn = nullptr;
    void* userData = nullptr;
    std::atomic<std::uint32_t> pendingDeps{0};
    std::vector<std::uint64_t> dependents;
    std::atomic<bool> completed{false};
    std::atomic<bool> scheduled{false};
};

// One named zone + logical-fiber scope per executed task (5.5.1e). Both the
// worker loop and the work-helping paths in wait()/waitAll() go through here,
// so external profilers see every task regardless of which thread ran it.
void runTask(Task* task)
{
    if (!task || !task->fn) return;
    SF_PROFILE_ZONE("job::execute");
    SF_PROFILE_FIBER_ENTER("job::task");
    task->fn(task->userData);
    SF_PROFILE_FIBER_EXIT();
}

} // namespace

struct JobScheduler::Impl
{
    SchedulerConfig config;
    std::vector<std::thread> workers;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::deque<Task*> readyQueue;
    std::atomic<bool> shutdown{false};
    std::atomic<std::uint64_t> nextTaskId{1};
    std::atomic<std::size_t> incompleteTasks{0};

    std::mutex tasksMutex;
    // Slot map: id -> task pointer for O(1) lookup
    std::unordered_map<std::uint64_t, Task*> taskMap;
    // Ownership
    std::vector<std::unique_ptr<Task>> ownedTasks;

    // Thread-local scratch is managed via file-scope thread_local blocks in
    // scratchAlloc()/resetScratch() to avoid pointer invalidation and locks.

    // Deferred deletion queue (epoch-based)
    struct RetiredItem
    {
        void* ptr = nullptr;
        void (*deleter)(void*) = nullptr;
        std::uint32_t retireEpoch = 0;
    };
    std::mutex retireMutex;
    std::vector<RetiredItem> retired;
    std::atomic<std::uint32_t> currentEpoch{0};

    Task* findTask(std::uint64_t id)
    {
        // Assumes tasksMutex is held by caller
        auto it = taskMap.find(id);
        return (it != taskMap.end()) ? it->second : nullptr;
    }

    void notifyDependentsAndEnqueue(Task* task)
    {
        if (!task) return;
        std::vector<Task*> toSchedule;
        {
            std::lock_guard lock(tasksMutex);
            for (std::uint64_t depId : task->dependents)
            {
                Task* dep = findTask(depId);
                if (!dep) continue;
                const std::uint32_t prev = dep->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1)
                {
                    bool expected = false;
                    if (dep->scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        toSchedule.push_back(dep);
                    }
                }
            }
            if (!toSchedule.empty())
            {
                std::lock_guard qlock(queueMutex);
                for (Task* dep : toSchedule)
                {
                    readyQueue.push_back(dep);
                }
            }
        }
        if (!toSchedule.empty())
        {
            cv.notify_all();
        }
    }

    void compactIfQuiescent(Task* keepAlive = nullptr)
    {
        if (incompleteTasks.load(std::memory_order_acquire) != 0) return;
        std::lock_guard taskLock(tasksMutex);
        std::lock_guard queueLock(queueMutex);
        if (incompleteTasks.load(std::memory_order_acquire) != 0 || !readyQueue.empty())
        {
            return;
        }

        std::unique_ptr<Task> preserved;
        if (keepAlive != nullptr)
        {
            for (auto& owned : ownedTasks)
            {
                if (owned.get() == keepAlive)
                {
                    preserved = std::move(owned);
                    break;
                }
            }
        }

        taskMap.clear();
        ownedTasks.clear();

        if (preserved)
        {
            Task* raw = preserved.get();
            taskMap[raw->id] = raw;
            ownedTasks.push_back(std::move(preserved));
        }
    }

    void completeTask(Task* task)
    {
        if (!task) return;
        const bool wasCompleted = task->completed.exchange(true, std::memory_order_acq_rel);
        if (wasCompleted) return;
        incompleteTasks.fetch_sub(1, std::memory_order_acq_rel);
        notifyDependentsAndEnqueue(task);
        cv.notify_all();
    }

    void workerLoop(std::uint32_t workerIndex)
    {
        const std::string threadName = std::format("SF Worker {}", workerIndex);
        SF_PROFILE_THREAD(threadName.c_str());

        while (true)
        {
            Task* task = nullptr;
            {
                std::unique_lock lock(queueMutex);
                cv.wait(lock, [this] { return shutdown.load() || !readyQueue.empty(); });
                if (shutdown.load() && readyQueue.empty()) return;
                if (!readyQueue.empty())
                {
                    task = readyQueue.front();
                    readyQueue.pop_front();
                }
            }

            if (task && !task->completed.load(std::memory_order_acquire))
            {
                runTask(task);
                completeTask(task);
            }
        }
    }
};

JobScheduler::JobScheduler(const SchedulerConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->config = config;
    std::uint32_t tc = config.threadCount;
    if (tc == 0)
    {
        tc = std::thread::hardware_concurrency();
        if (tc > 1) --tc;
        if (tc == 0) tc = 1;
    }

    if (config.mode != SchedulerMode::SingleThreaded)
    {
        for (std::uint32_t i = 0; i < tc; ++i)
        {
            impl_->workers.emplace_back([this, i] { impl_->workerLoop(i); });
        }
    }
}

JobScheduler::~JobScheduler()
{
    // Drain outstanding work before tearing down: destroying userData
    // while tasks are still in flight would race with their execution.
    waitAll();

    {
        std::lock_guard lock(impl_->queueMutex);
        impl_->shutdown.store(true);
    }
    impl_->cv.notify_all();
    for (auto& w : impl_->workers)
    {
        if (w.joinable()) w.join();
    }
}

TaskHandle JobScheduler::submit(TaskFn fn, void* userData)
{
    return submit(fn, userData, std::span<const TaskHandle>{});
}

TaskHandle JobScheduler::submit(TaskFn fn, void* userData,
                               std::span<const TaskHandle> dependencies)
{
    auto task = std::make_unique<Task>();
    task->id = impl_->nextTaskId.fetch_add(1);
    task->fn = fn;
    task->userData = userData;

    std::uint32_t alreadyDone = 0;
    {
        std::lock_guard lock(impl_->tasksMutex);
        task->pendingDeps.store(static_cast<std::uint32_t>(dependencies.size()), std::memory_order_relaxed);

        for (auto dep : dependencies)
        {
            Task* depTask = impl_->findTask(dep.id);
            if (depTask)
            {
                if (depTask->completed.load(std::memory_order_acquire))
                {
                    ++alreadyDone;
                }
                else
                {
                    depTask->dependents.push_back(task->id);
                }
            }
            else
            {
                ++alreadyDone;
            }
        }
        task->pendingDeps.store(static_cast<std::uint32_t>(dependencies.size()) - alreadyDone, std::memory_order_relaxed);

        Task* rawTask = task.get();
        impl_->taskMap[task->id] = rawTask;
        impl_->ownedTasks.push_back(std::move(task)); // keep for lifetime
        impl_->incompleteTasks.fetch_add(1, std::memory_order_relaxed);

        // Schedule if ready
        if (rawTask->pendingDeps.load(std::memory_order_relaxed) == 0)
        {
            bool expected = false;
            if (rawTask->scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                std::lock_guard qlock(impl_->queueMutex);
                impl_->readyQueue.push_back(rawTask);
                impl_->cv.notify_one();
            }
        }

        return TaskHandle{rawTask->id};
    }
}

void JobScheduler::wait(TaskHandle handle)
{
    if (impl_->config.mode == SchedulerMode::SingleThreaded)
    {
        // SingleThreaded/debug mode: execute ready tasks on the calling thread.
        // Uses the ready queue (O(executed work)) rather than scanning all tasks.
        while (true)
        {
            bool done = false;
            {
                std::lock_guard lock(impl_->tasksMutex);
                Task* target = impl_->findTask(handle.id);
                done = !target || target->completed.load(std::memory_order_acquire);
            }
            if (done) return;

            Task* next = nullptr;
            {
                std::lock_guard lock(impl_->queueMutex);
                if (!impl_->readyQueue.empty())
                {
                    next = impl_->readyQueue.front();
                    impl_->readyQueue.pop_front();
                }
            }
            if (!next) return;
            if (!next->completed.load(std::memory_order_acquire))
            {
                runTask(next);
                impl_->completeTask(next);
            }
        }
    }

    while (true)
    {
        bool done = false;
        {
            std::lock_guard lock(impl_->tasksMutex);
            Task* target = impl_->findTask(handle.id);
            done = !target || target->completed.load(std::memory_order_acquire);
        }
        if (done) return;

        Task* toRun = nullptr;
        {
            std::unique_lock lock(impl_->queueMutex);
            if (!impl_->readyQueue.empty())
            {
                toRun = impl_->readyQueue.front();
                impl_->readyQueue.pop_front();
            }
        }
        if (toRun && !toRun->completed.load(std::memory_order_acquire))
        {
            runTask(toRun);
            impl_->completeTask(toRun);
        }
        else
        {
            std::unique_lock lock(impl_->queueMutex);
            impl_->cv.wait_for(lock, std::chrono::microseconds(100), [this] {
                return impl_->shutdown.load(std::memory_order_acquire) ||
                       !impl_->readyQueue.empty() ||
                       impl_->incompleteTasks.load(std::memory_order_acquire) == 0;
            });
        }
    }
}

void JobScheduler::waitAll()
{
    if (impl_->config.mode == SchedulerMode::SingleThreaded)
    {
        // Execute all pending tasks, propagating dependencies so newly-ready
        // tasks are enqueued and drained in the same pass.
        bool progress = true;
        while (progress)
        {
            progress = false;
            std::vector<Task*> toRun;
            {
                std::lock_guard lock(impl_->queueMutex);
                while (!impl_->readyQueue.empty())
                {
                    toRun.push_back(impl_->readyQueue.front());
                    impl_->readyQueue.pop_front();
                }
            }

            for (Task* t : toRun)
            {
                if (t && !t->completed.load(std::memory_order_acquire))
                {
                    runTask(t);
                    impl_->completeTask(t);
                    progress = true;
                    if (impl_->incompleteTasks.load(std::memory_order_acquire) == 0)
                    {
                        break;
                    }
                }
            }
        }

        impl_->compactIfQuiescent();
        return;
    }

    while (impl_->incompleteTasks.load(std::memory_order_acquire) > 0)
    {
        Task* toRun = nullptr;
        {
            std::unique_lock lock(impl_->queueMutex);
            if (!impl_->readyQueue.empty())
            {
                toRun = impl_->readyQueue.front();
                impl_->readyQueue.pop_front();
            }
        }
        if (toRun && !toRun->completed.load(std::memory_order_acquire))
        {
            runTask(toRun);
            impl_->completeTask(toRun);
        }
        else if (impl_->incompleteTasks.load(std::memory_order_acquire) > 0)
        {
            std::unique_lock lock(impl_->queueMutex);
            impl_->cv.wait_for(lock, std::chrono::microseconds(100), [this] {
                return impl_->shutdown.load(std::memory_order_acquire) ||
                       !impl_->readyQueue.empty() ||
                       impl_->incompleteTasks.load(std::memory_order_acquire) == 0;
            });
        }
    }

    impl_->compactIfQuiescent();
}

std::uint32_t JobScheduler::threadCount() const noexcept
{
    return static_cast<std::uint32_t>(impl_->workers.empty() ? 1 : impl_->workers.size());
}

bool JobScheduler::isDeterministic() const noexcept
{
    return impl_->config.mode == SchedulerMode::Deterministic;
}

// File-scope thread-local scratch blocks
struct ScratchBlock
{
    std::vector<std::byte> memory;
    std::size_t offset = 0;
};
thread_local std::vector<ScratchBlock> t_scratchBlocks;

void* JobScheduler::scratchAlloc(std::size_t size, std::size_t alignment)
{
    // Thread-local scratch using chunked blocks to avoid pointer invalidation.
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }
    if (size > (std::numeric_limits<std::size_t>::max() - alignment))
    {
        return nullptr;
    }

    const auto alignUp = [alignment](std::uintptr_t addr) -> std::uintptr_t {
        return (addr + alignment - 1) & ~(alignment - 1);
    };

    // Find a block with enough space
    for (auto& block : t_scratchBlocks)
    {
        const auto baseAddr = reinterpret_cast<std::uintptr_t>(block.memory.data() + block.offset);
        const std::uintptr_t alignedAddr = alignUp(baseAddr);
        const std::size_t padding = static_cast<std::size_t>(alignedAddr - baseAddr);
        if (padding <= block.memory.size() - block.offset &&
            size <= block.memory.size() - block.offset - padding)
        {
            block.offset += padding + size;
            return reinterpret_cast<void*>(alignedAddr);
        }
    }

    // Allocate new block
    ScratchBlock newBlock;
    const std::size_t blockSize = std::max(size + alignment, static_cast<std::size_t>(65536));
    newBlock.memory.resize(blockSize);
    const auto baseAddr = reinterpret_cast<std::uintptr_t>(newBlock.memory.data());
    const std::uintptr_t alignedAddr = alignUp(baseAddr);
    const std::size_t padding = static_cast<std::size_t>(alignedAddr - baseAddr);
    void* ptr = reinterpret_cast<void*>(alignedAddr);
    newBlock.offset = padding + size;
    t_scratchBlocks.push_back(std::move(newBlock));
    return ptr;
}

void JobScheduler::resetScratch()
{
    for (auto& b : t_scratchBlocks)
    {
        b.offset = 0;
    }
}

void JobScheduler::retire(void* ptr, void (*deleter)(void*))
{
    std::lock_guard lock(impl_->retireMutex);
    impl_->retired.push_back({ptr, deleter, impl_->currentEpoch.load()});
}

void JobScheduler::collectRetired(std::uint32_t currentEpoch)
{
    impl_->currentEpoch.store(currentEpoch);
    std::vector<Impl::RetiredItem> toDelete;
    {
        std::lock_guard lock(impl_->retireMutex);
        auto it = impl_->retired.begin();
        while (it != impl_->retired.end())
        {
            if (currentEpoch >= it->retireEpoch + 2) // 2-frame delay
            {
                toDelete.push_back(*it);
                it = impl_->retired.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (auto& item : toDelete)
    {
        if (item.deleter && item.ptr)
        {
            item.deleter(item.ptr);
        }
    }
}

namespace
{
std::unique_ptr<JobScheduler> g_scheduler;
std::mutex g_schedulerMutex;
}

JobScheduler& globalScheduler()
{
    std::lock_guard lock(g_schedulerMutex);
    if (!g_scheduler)
    {
        g_scheduler = std::make_unique<JobScheduler>(SchedulerConfig{});
    }
    return *g_scheduler;
}

void initGlobalScheduler(const SchedulerConfig& config)
{
    std::lock_guard lock(g_schedulerMutex);
    g_scheduler = std::make_unique<JobScheduler>(config);
}

void shutdownGlobalScheduler()
{
    std::lock_guard lock(g_schedulerMutex);
    g_scheduler.reset();
}

} // namespace starforge::jobs
