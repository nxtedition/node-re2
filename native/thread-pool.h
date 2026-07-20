#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace node_re2 {

class ThreadPool {
 public:
  using Callback = void (*)(const void*, size_t) noexcept;

  explicit ThreadPool(size_t worker_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void Run(size_t thread_count, size_t work_count, const void* context, Callback callback);

 private:
  void ProcessWork() noexcept;
  void WorkerLoop(size_t worker_index) noexcept;
  void Stop() noexcept;

  std::mutex submission_mutex_;
  std::vector<std::thread> workers_;
  std::atomic<bool> running_{true};
  std::atomic<uint64_t> job_state_{0};
  std::atomic<size_t> remaining_workers_{0};
  std::atomic<size_t> next_work_{0};
  size_t work_count_ = 0;
  const void* context_ = nullptr;
  Callback callback_ = nullptr;
};

ThreadPool& GetBatchThreadPool();
bool RetainBatchThreadPoolEnvironment() noexcept;
void ReleaseBatchThreadPoolEnvironment() noexcept;

template <typename Function>
void RunOnBatchThreadPool(size_t thread_count, size_t work_count, Function&& function) {
  using FunctionType = std::remove_cvref_t<Function>;
  static_assert(noexcept(std::declval<const FunctionType&>()(size_t{})));
  GetBatchThreadPool().Run(
      thread_count, work_count, &function,
      [](const void* context, size_t index) noexcept {
        (*static_cast<const FunctionType*>(context))(index);
      });
}

}  // namespace node_re2
