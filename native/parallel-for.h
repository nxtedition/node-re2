#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <limits>

#include "batch-plan.h"
#include "thread-pool.h"

namespace node_re2 {

template <typename Function>
void ParallelFor(size_t size, size_t total_bytes, Function&& function) {
  const size_t thread_count = BatchParallelism(size, total_bytes);
#ifdef NODE_RE2_PARALLEL
  if (thread_count > 1) {
#ifdef NODE_RE2_BATCHES_PER_THREAD
    constexpr size_t kBatchesPerThread = NODE_RE2_BATCHES_PER_THREAD;
#else
    constexpr size_t kBatchesPerThread = 16;
#endif
    static_assert(kBatchesPerThread > 0);
    const size_t requested_batches =
        thread_count > std::numeric_limits<size_t>::max() / kBatchesPerThread
            ? size
            : thread_count * kBatchesPerThread;
    const size_t batch_count = std::min(size, requested_batches);
    const size_t batch_size = 1 + ((size - 1) / batch_count);
    const size_t work_count = 1 + ((size - 1) / batch_size);
    std::atomic<bool> stopped{false};
    std::exception_ptr error;
    const auto run_batch = [&](size_t batch) noexcept {
      if (stopped.load(std::memory_order_relaxed)) {
        return;
      }
      try {
        const size_t begin = batch * batch_size;
        const size_t end = begin + std::min(batch_size, size - begin);
        for (size_t index = begin; index < end; ++index) {
          function(index);
        }
      } catch (...) {
        if (!stopped.exchange(true, std::memory_order_acq_rel)) {
          error = std::current_exception();
        }
      }
    };

    RunOnBatchThreadPool(thread_count, work_count, run_batch);
    if (error != nullptr) {
      std::rethrow_exception(error);
    }
    return;
  }
#else
  (void)thread_count;
#endif

  for (size_t index = 0; index < size; ++index) {
    function(index);
  }
}

}  // namespace node_re2
