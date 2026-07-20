#include "thread-pool.h"

#include <limits>
#include <memory>
#include <stdexcept>

#include "batch-plan.h"

namespace node_re2 {
namespace {

constexpr uint64_t kActiveWorkerMask = UINT64_C(0xffffffff);
constexpr uint64_t kGenerationIncrement = kActiveWorkerMask + 1;

struct BatchThreadPoolState {
  std::mutex mutex;
  std::unique_ptr<ThreadPool> pool;
  size_t environment_count = 0;
};

BatchThreadPoolState& GetBatchThreadPoolState() {
  static BatchThreadPoolState state;
  return state;
}

}  // namespace

ThreadPool::ThreadPool(size_t worker_count) {
  if (worker_count > kActiveWorkerMask) {
    throw std::invalid_argument("Too many batch thread-pool workers");
  }
  workers_.reserve(worker_count);
  try {
    for (size_t index = 0; index < worker_count; ++index) {
      workers_.emplace_back([this, index] { WorkerLoop(index); });
    }
  } catch (...) {
    Stop();
    throw;
  }
}

ThreadPool::~ThreadPool() {
  std::lock_guard lock(submission_mutex_);
  Stop();
}

void ThreadPool::Run(size_t thread_count, size_t work_count, const void* context, Callback callback) {
  if (thread_count < 2 || thread_count > workers_.size() + 1 || work_count == 0 || context == nullptr ||
      callback == nullptr) {
    throw std::invalid_argument("Invalid batch thread-pool work");
  }

  std::lock_guard lock(submission_mutex_);
  work_count_ = work_count;
  context_ = context;
  callback_ = callback;
  next_work_.store(0, std::memory_order_relaxed);
  remaining_workers_.store(thread_count - 1, std::memory_order_relaxed);

  const uint64_t previous_state = job_state_.load(std::memory_order_relaxed);
  const uint64_t next_state =
      ((previous_state & ~kActiveWorkerMask) + kGenerationIncrement) | static_cast<uint64_t>(thread_count - 1);
  job_state_.store(next_state, std::memory_order_release);
  job_state_.notify_all();
  ProcessWork();

  size_t remaining = remaining_workers_.load(std::memory_order_acquire);
  while (remaining != 0) {
    remaining_workers_.wait(remaining, std::memory_order_acquire);
    remaining = remaining_workers_.load(std::memory_order_acquire);
  }

  callback_ = nullptr;
  context_ = nullptr;
  work_count_ = 0;
}

void ThreadPool::ProcessWork() noexcept {
  while (true) {
    const size_t index = next_work_.fetch_add(1, std::memory_order_relaxed);
    if (index >= work_count_) {
      return;
    }
    callback_(context_, index);
  }
}

void ThreadPool::WorkerLoop(size_t worker_index) noexcept {
  uint64_t observed_state = 0;
  while (true) {
    uint64_t state = job_state_.load(std::memory_order_acquire);
    while (state == observed_state) {
      job_state_.wait(observed_state, std::memory_order_acquire);
      state = job_state_.load(std::memory_order_acquire);
    }
    observed_state = state;

    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    const size_t active_workers = static_cast<size_t>(state & kActiveWorkerMask);
    if (worker_index >= active_workers) {
      continue;
    }

    ProcessWork();
    if (remaining_workers_.fetch_sub(1, std::memory_order_release) == 1) {
      remaining_workers_.notify_one();
    }
  }
}

void ThreadPool::Stop() noexcept {
  running_.store(false, std::memory_order_release);
  job_state_.fetch_add(kGenerationIncrement, std::memory_order_release);
  job_state_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

ThreadPool& GetBatchThreadPool() {
  BatchThreadPoolState& state = GetBatchThreadPoolState();
  std::lock_guard lock(state.mutex);
  if (state.environment_count == 0) {
    throw std::logic_error("Batch thread pool has no active Node.js environment");
  }
  if (state.pool == nullptr) {
    state.pool = std::make_unique<ThreadPool>(MaxBatchParallelism() - 1);
  }
  return *state.pool;
}

bool RetainBatchThreadPoolEnvironment() noexcept {
  try {
    BatchThreadPoolState& state = GetBatchThreadPoolState();
    std::lock_guard lock(state.mutex);
    if (state.environment_count == std::numeric_limits<size_t>::max()) {
      return false;
    }
    ++state.environment_count;
    return true;
  } catch (...) {
    return false;
  }
}

void ReleaseBatchThreadPoolEnvironment() noexcept {
  std::unique_ptr<ThreadPool> pool;
  try {
    BatchThreadPoolState& state = GetBatchThreadPoolState();
    {
      std::lock_guard lock(state.mutex);
      if (state.environment_count == 0) {
        return;
      }
      --state.environment_count;
      if (state.environment_count == 0) {
        pool = std::move(state.pool);
      }
    }
  } catch (...) {
  }
}

}  // namespace node_re2
