# ADR 0008: Custom Work-Stealing Job Scheduler vs. Third-Party Library

## Status

Accepted (with timebox)

## Context

Milestone M5.5 introduces multi-threaded execution across procedural content
generation, spatial updates, and Vulkan command recording. The workload
exhibits diverse characteristics:

- **Fine-grained, embarrassingly parallel tasks:** Terrain heightfield
  evaluations, noise sampling, atmosphere ray-marching.
- **Coarse, graph-dependent tasks:** Chunk content generation, delta
  resolution, quadtree splits.
- **Specialized execution requirements:** Pure functional determinism,
  thread-local scratch linear allocators, counter-based task dependencies,
  and continuation-passing style.

Existing third-party libraries (enkiTS, Taskflow, TBB) provide robust
schedulers, but introduce tradeoffs:

- Taskflow relies on modern C++ templates with heavier compilation overhead
  and abstract task allocations.
- enkiTS is lightweight and C-compatible, but lacks native fine-grained
  task continuation graphs and counter-based dependency semantics without
  custom wrapper layers.
- Third-party libraries complicate deterministic multi-threaded test
  harnesses where task ordering, synthetic yields, or single-step stepping
  must be injected.

## Decision

We will implement a lightweight, custom C++20 work-stealing job system based
on the Chase-Lev deque algorithm, coupled with an atomic counter-based
dependency graph and continuation scheduler.

To safeguard the engineering budget, this task is **timeboxed to 5 working
days**. If core stability (ThreadSanitizer validation and stress benchmarks)
is not met within this window, the scheduler will be swapped out for enkiTS,
retaining our task graph abstraction layer as a frontend.

## Technical Specifications & Implementation Constraints

### 1. Bounded Deques Over Concurrent Resizing

Dynamic resizing in lock-free Chase-Lev deques requires complex epoch-based
memory reclamation or hazard pointers to prevent data races between the owner
pushing and a stealer reading the buffer pointer.

**Constraint:** Use a fixed power-of-two bounded ring buffer (e.g., 2,048 or
4,096 task pointers) per worker thread.

**Overflow Handling:** If a worker's local deque overflows, overflow tasks
spill into a shared multi-producer multi-consumer (MPMC) locked fallback
queue rather than triggering concurrent deque resizing.

### 2. Strict C++20 Memory Ordering

To avoid subtle races on the single-producer multi-consumer boundary:

- **push():** Writes element to bottom, followed by
  `std::atomic_thread_fence(std::memory_order_release)` or
  `bottom.store(..., std::memory_order_release)`.
- **pop() (owner):** Decrements bottom (`std::memory_order_seq_cst`). If
  `bottom < top`, deque was empty. If `bottom == top`, the last item is
  contested with a stealer: execute a CAS (`compare_exchange_strong`) on
  top using `std::memory_order_seq_cst`.
- **steal() (stealer):** Reads top (`std::memory_order_acquire`), verifies
  `top < bottom`, reads the task, and executes a CAS on top
  (`std::memory_order_seq_cst`).

### 3. Counter-Based Continuations

Dependent tasks hold an atomic decrement counter
`std::atomic<std::uint32_t> pendingDependencies`.

Completing tasks invoke `fetch_sub(1, std::memory_order_acq_rel)`. The
thread that transitions the counter from 1 to 0 immediately pushes the
continuation task to the head (LIFO) of its local deque to maximize cache
locality on intermediate working data.

### 4. Deterministic Testing Mode

The scheduler must include a compilation/runtime toggle:

When running in deterministic test mode (`SchedulerMode::Deterministic`),
task stealing is disabled or follows an explicit pseudo-random sequence
seeded per run, and workers yield deterministically to verify reproducibility
across identical inputs.

## Exit Criteria & Fallback Plan

If any of the following occur during the 5-day window, switch immediately
to enkiTS:

1. Persistent data races or deadlocks detected by ThreadSanitizer (5.5.6a)
   that cannot be resolved within 1 business day.
2. Scheduler overhead consumes > 15% of total frame time in the
   micro-benchmark suite (5.5.6c) compared to a single-threaded baseline.
3. Implementation exceeds the 5-day budget before task graph continuations
   are operational.

## Public C++ API (Proposed)

```cpp
namespace starforge::jobs
{

enum class SchedulerMode
{
    Normal,        // Work-stealing enabled
    Deterministic, // No stealing, seeded yields
    SingleThreaded // No worker threads (debugging)
};

// Handle to a submitted task.
struct TaskHandle
{
    std::uint64_t id = 0;
    bool isValid() const noexcept { return id != 0; }
};

// Task function signature.
using TaskFn = void(*)(void* userData);

// Scheduler configuration.
struct SchedulerConfig
{
    std::uint32_t threadCount = 0; // 0 = hardware_concurrency() - 1
    SchedulerMode mode = SchedulerMode::Normal;
    std::uint32_t dequeCapacity = 4096; // power of two
};

// The job scheduler.
class JobScheduler
{
public:
    explicit JobScheduler(const SchedulerConfig& config = {});
    ~JobScheduler();

    // Submit a task with optional dependencies.
    // Returns a handle that can be used as a dependency for other tasks.
    TaskHandle submit(TaskFn fn, void* userData = nullptr,
                      std::span<const TaskHandle> dependencies = {});

    // Submit a task and return a handle that completes when the task finishes.
    // The continuation is automatically scheduled when all dependencies resolve.
    TaskHandle submitContinuation(TaskFn fn, void* userData = nullptr,
                                  std::span<const TaskHandle> dependencies = {});

    // Wait for a specific task to complete.
    void wait(TaskHandle handle);

    // Wait for all submitted tasks to complete.
    void waitAll();

    // Get the number of worker threads.
    std::uint32_t threadCount() const noexcept;

    // Get thread-local scratch allocator for the current thread.
    // Returns nullptr if called outside a worker thread.
    void* scratchAlloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    // Reset thread-local scratch allocator (called automatically after each task).
    void resetScratch();

    // Check if running in deterministic mode.
    bool isDeterministic() const noexcept;

private:
    // Implementation details (pimpl or template)
};

// Global scheduler instance (initialized at engine startup).
JobScheduler& globalScheduler();

// Convenience: submit to global scheduler.
inline TaskHandle submit(TaskFn fn, void* userData = nullptr,
                        std::span<const TaskHandle> dependencies = {})
{
    return globalScheduler().submit(fn, userData, dependencies);
}

} // namespace starforge::jobs
```

## Consequences

- **Positive:** Full control over determinism, memory ordering, and profiling.
  Task graph semantics match engine requirements exactly.
- **Negative:** 5-day implementation risk. Fallback to enkiTS if unstable.
- **Mitigation:** Task graph abstraction layer is library-agnostic; swapping
  scheduler backends does not change call sites.

## Related

- M5.5: Job System & Multi-Threading Foundation
- ADR 0004: Performance Budgets
- ADR 0005: Math Library
