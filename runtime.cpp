#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace {

// Process-wide worker pool used by async/sync.
class AsyncRuntime {
  // Shared state.
  std::mutex mutex;
  // Signals workers when tasks are queued.
  std::condition_variable workAvailable;
  // Signals sync() when all work is done.
  std::condition_variable workFinished;
  // Pending work items.
  std::queue<std::function<void()>> tasks;
  // Pool threads.
  std::vector<std::thread> workers;
  // Number of unfinished tasks.
  std::size_t pendingTasks = 0;
  // Set during teardown.
  bool shuttingDown = false;

  void workerLoop() {
    while (true) {
      std::function<void()> task;

      {
        std::unique_lock<std::mutex> lock(mutex);
        // Wait for work or shutdown.
        workAvailable.wait(lock,
                           [this] { return shuttingDown || !tasks.empty(); });

        // Exit once shutdown starts and no work remains.
        if (shuttingDown && tasks.empty()) {
          return;
        }

        // Take one task from the queue.
        task = std::move(tasks.front());
        tasks.pop();
      }

      // Run work outside the lock.
      task();

      {
        std::lock_guard<std::mutex> lock(mutex);
        // Wake sync() when the last task finishes.
        --pendingTasks;
        if (pendingTasks == 0) {
          workFinished.notify_all();
        }
      }
    }
  }

public:
  AsyncRuntime() {
    std::size_t workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0) {
      // Fallback when the platform gives no hint.
      workerCount = 2;
    }

    for (std::size_t i = 0; i < workerCount; ++i) {
      workers.emplace_back([this] { workerLoop(); });
    }
  }

  ~AsyncRuntime() {
    // Finish work before tearing down the pool.
    sync();

    {
      std::lock_guard<std::mutex> lock(mutex);
      shuttingDown = true;
    }
    // Wake idle workers so they can exit.
    workAvailable.notify_all();

    for (auto &worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  void enqueue(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      // Count work as pending when it is queued.
      ++pendingTasks;
      tasks.push(std::move(task));
    }
    // Wake one worker.
    workAvailable.notify_one();
  }

  void sync() {
    std::unique_lock<std::mutex> lock(mutex);
    // Wait until no tasks remain.
    workFinished.wait(lock, [this] { return pendingTasks == 0; });
  }

  std::size_t workerCount() const { return workers.size(); }

  void parallelFor(void (*task)(void *, std::size_t, std::size_t), void *data,
                   double start, double end, double step) {
    if (!(step > 0.0)) {
      std::fprintf(stderr, "Error: parfor step must be greater than 0\n");
      return;
    }
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step)) {
      std::fprintf(stderr, "Error: parfor bounds must be finite\n");
      return;
    }
    if (!(end > start)) {
      return;
    }

    double span = end - start;
    std::size_t iterations =
        static_cast<std::size_t>(std::ceil(span / step));
    if (iterations == 0) {
      return;
    }

    std::size_t desiredChunks = std::max<std::size_t>(1, workerCount() * 4);
    std::size_t chunkCount = std::min(iterations, desiredChunks);
    std::size_t grainSize = (iterations + chunkCount - 1) / chunkCount;

    struct CompletionGroup {
      std::mutex mutex;
      std::condition_variable finished;
      std::size_t pending = 0;
    } group;

    for (std::size_t begin = 0; begin < iterations; begin += grainSize) {
      std::size_t chunkEnd = std::min(iterations, begin + grainSize);
      {
        std::lock_guard<std::mutex> lock(group.mutex);
        ++group.pending;
      }

      enqueue([task, data, begin, chunkEnd, &group] {
        task(data, begin, chunkEnd);
        std::lock_guard<std::mutex> lock(group.mutex);
        --group.pending;
        if (group.pending == 0) {
          group.finished.notify_all();
        }
      });
    }

    std::unique_lock<std::mutex> lock(group.mutex);
    group.finished.wait(lock, [&group] { return group.pending == 0; });
  }
};

AsyncRuntime &getRuntime() {
  // Single shared runtime instance.
  static AsyncRuntime runtime;
  return runtime;
}

} // namespace

extern "C" double __compiler_sync_tasks() {
  // Runtime entry point for sync().
  getRuntime().sync();
  return 0.0;
}

extern "C" double __compiler_async_call(void (*task)(void *), void *data) {
  // Runtime entry point for async
  getRuntime().enqueue([task, data] { task(data); });
  return 0.0;
}

extern "C" double __compiler_parfor(
    void (*task)(void *, std::size_t, std::size_t), void *data, double start,
    double end, double step) {
  getRuntime().parallelFor(task, data, start, end, step);
  return 0.0;
}
