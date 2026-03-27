#include <condition_variable>
#include <cstddef>
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

extern "C" double __compiler_async_0(double (*func)()) {
  // Queue a zero-argument call.
  getRuntime().enqueue([func] { func(); });
  return 0.0;
}

extern "C" double __compiler_async_1(double (*func)(double), double arg0) {
  // Queue a one-argument call.
  getRuntime().enqueue([func, arg0] { func(arg0); });
  return 0.0;
}

extern "C" double __compiler_async_2(double (*func)(double, double),
                                     double arg0, double arg1) {
  // Queue a two-argument call.
  getRuntime().enqueue([func, arg0, arg1] { func(arg0, arg1); });
  return 0.0;
}

extern "C" double __compiler_async_3(double (*func)(double, double, double),
                                     double arg0, double arg1, double arg2) {
  // Queue a three-argument call.
  getRuntime().enqueue([func, arg0, arg1, arg2] { func(arg0, arg1, arg2); });
  return 0.0;
}
