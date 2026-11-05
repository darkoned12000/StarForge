#include "jobs/job_scheduler.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
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

    std::mutex tasksMutex;
    // Slot map: id -> task pointer for O(1) lookup
    std::unordered_map<std::uint64_t, Task*> taskMap;
    // Ownership
    std::vector<std::unique_ptr<Task>> ownedTasks;

    // Thread-local scratch: chunked block allocation to avoid use-after-free
    struct ScratchBlock
    {
        std::vector<std::byte> memory;
        std::size_t offset = 0;
    };
    struct ThreadScratch
    {
        std::vector<ScratchBlock> blocks;
    };
    std::mutex scratchMutex;
    std::unordered_map<std::thread::id, ThreadScratch> scratches;

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

    void workerLoop(std::uint32_t /*workerIndex*/)
    {
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

            if (task)
            {
                if (task->fn)
                {
                    task->fn(task->userData);
                }
                task->completed.store(true, std::memory_order_release);

                // Notify dependents
                std::vector<Task*> toSchedule;
                {
                    std::lock_guard lock(tasksMutex);
                    for (std::uint64_t depId : task->dependents)
                    {
                        Task* dep = findTask(depId);
                        if (dep)
                        {
                            std::uint32_t prev = dep->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                            if (prev == 1)
                            {
                                bool expected = false;
                                if (dep->scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                                {
                                    toSchedule.push_back(dep);
                                }
                            }
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
                    cv.notify_all();
                }
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
        // Recursive execution with dependency resolution
        std::function<void(Task*)> execute = [&](Task* t) {
            if (!t || t->completed.load()) return;
            // Execute dependencies first
            // Find tasks that this task depends on (reverse lookup)
            // Simple approach: if pendingDeps > 0, we can't execute yet
            // Instead, execute all ready tasks in topological order
        };

        // Simpler: drain readyQueue and execute, then retry
        Task* target = nullptr;
        {
            std::lock_guard lock(impl_->tasksMutex);
            target = impl_->findTask(handle.id);
        }
        if (!target) return;

        bool progress = true;
        while (progress && !target->completed.load())
        {
            progress = false;
            Task* t = target;
            if (!t || t->completed.load()) break;

            if (t->pendingDeps.load() > 0)
            {
                // Need to execute dependencies first - find them
                // Dependencies are tracked in reverse (dependents), so we need
                // to find tasks that t depends on. Since we don't store
                // dependencies list in Task, execute all uncompleted tasks
                // with 0 pendingDeps first.
                std::vector<Task*> ready;
                {
                    std::lock_guard lock(impl_->tasksMutex);
                    for (auto& [id, task] : impl_->taskMap)
                    {
                        if (task && !task->completed.load() && task->pendingDeps.load() == 0 && task->id != t->id)
                        {
                            ready.push_back(task);
                        }
                    }
                }
                for (Task* dep : ready)
                {
                    if (dep->fn) dep->fn(dep->userData);
                    dep->completed.store(true, std::memory_order_release);
                    // Notify
                    {
                        std::lock_guard lock(impl_->tasksMutex);
                        for (std::uint64_t depId : dep->dependents)
                        {
                            Task* d = impl_->findTask(depId);
                            if (d) d->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                        }
                    }
                    progress = true;
                }
                if (!ready.empty()) continue;
            }

            if (t->pendingDeps.load() == 0 && !t->completed.load())
            {
                if (t->fn) t->fn(t->userData);
                t->completed.store(true, std::memory_order_release);
                {
                    std::lock_guard lock(impl_->tasksMutex);
                    for (std::uint64_t depId : t->dependents)
                    {
                        Task* d = impl_->findTask(depId);
                        if (d) d->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                    }
                }
                progress = true;
            }
        }
        return;
    }

    Task* t = impl_->findTask(handle.id);
    // Need lock
    {
        std::lock_guard lock(impl_->tasksMutex);
        t = impl_->findTask(handle.id);
    }
    if (!t) return;
    while (!t->completed.load(std::memory_order_acquire))
    {
        // Work-helping: try to execute a task from the queue
        Task* toRun = nullptr;
        {
            std::unique_lock lock(impl_->queueMutex);
            if (!impl_->readyQueue.empty())
            {
                toRun = impl_->readyQueue.front();
                impl_->readyQueue.pop_front();
            }
        }
        if (toRun && !toRun->completed.load())
        {
            if (toRun->fn) toRun->fn(toRun->userData);
            toRun->completed.store(true, std::memory_order_release);

            // Notify dependents
            std::vector<Task*> toSchedule;
            {
                std::lock_guard lock(impl_->tasksMutex);
                for (std::uint64_t depId : toRun->dependents)
                {
                    Task* dep = impl_->findTask(depId);
                    if (dep)
                    {
                        std::uint32_t prev = dep->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                        if (prev == 1)
                        {
                            bool expected = false;
                            if (dep->scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                            {
                                toSchedule.push_back(dep);
                            }
                        }
                    }
                }
            }
            if (!toSchedule.empty())
            {
                std::lock_guard qlock(impl_->queueMutex);
                for (Task* dep : toSchedule)
                {
                    impl_->readyQueue.push_back(dep);
                }
                impl_->cv.notify_all();
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

void JobScheduler::waitAll()
{
    if (impl_->config.mode == SchedulerMode::SingleThreaded)
    {
        // Execute all pending tasks
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
                if (t && !t->completed.load())
                {
                    if (t->fn) t->fn(t->userData);
                    t->completed.store(true, std::memory_order_release);
                    progress = true;
                }
            }
        }
        return;
    }

    bool done = false;
    while (!done)
    {
        done = true;
        {
            std::lock_guard lock(impl_->tasksMutex);
            for (auto& [id, t] : impl_->taskMap)
            {
                if (t && !t->completed.load(std::memory_order_acquire))
                {
                    done = false;
                    break;
                }
            }
        }
        if (!done)
        {
            // Work-helping
            Task* toRun = nullptr;
            {
                std::unique_lock lock(impl_->queueMutex);
                if (!impl_->readyQueue.empty())
                {
                    toRun = impl_->readyQueue.front();
                    impl_->readyQueue.pop_front();
                }
            }
            if (toRun && !toRun->completed.load())
            {
                if (toRun->fn) toRun->fn(toRun->userData);
                toRun->completed.store(true, std::memory_order_release);

                std::vector<Task*> toSchedule;
                {
                    std::lock_guard lock(impl_->tasksMutex);
                    for (std::uint64_t depId : toRun->dependents)
                    {
                        Task* dep = impl_->findTask(depId);
                        if (dep)
                        {
                            std::uint32_t prev = dep->pendingDeps.fetch_sub(1, std::memory_order_acq_rel);
                            if (prev == 1)
                            {
                                bool expected = false;
                                if (dep->scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                                {
                                    toSchedule.push_back(dep);
                                }
                            }
                        }
                    }
                }
                if (!toSchedule.empty())
                {
                    std::lock_guard qlock(impl_->queueMutex);
                    for (Task* dep : toSchedule)
                    {
                        impl_->readyQueue.push_back(dep);
                    }
                    impl_->cv.notify_all();
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
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
    // Thread-local scratch using chunked blocks to avoid pointer invalidation

    // Find a block with enough space
    for (auto& block : t_scratchBlocks)
    {
        auto baseAddr = reinterpret_cast<std::uintptr_t>(block.memory.data() + block.offset);
        std::uintptr_t alignedAddr = (baseAddr + alignment - 1) & ~(alignment - 1);
        std::size_t padding = alignedAddr - baseAddr;
        if (block.offset + padding + size <= block.memory.size())
        {
            block.offset += padding + size;
            return reinterpret_cast<void*>(alignedAddr);
        }
    }

    // Allocate new block
    ScratchBlock newBlock;
    std::size_t blockSize = std::max(size + alignment, static_cast<std::size_t>(65536));
    newBlock.memory.resize(blockSize);
    auto baseAddr = reinterpret_cast<std::uintptr_t>(newBlock.memory.data());
    std::uintptr_t alignedAddr = (baseAddr + alignment - 1) & ~(alignment - 1);
    std::size_t padding = alignedAddr - baseAddr;
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
