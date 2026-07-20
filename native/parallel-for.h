#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>

#include "batch-plan.h"
#include "thread-pool.h"

namespace node_re2 {

template <typename Function>
void ParallelFor(size_t size, size_t total_bytes, size_t requested_batch_size, Function&& function) {
  const BatchPlan plan = MakeBatchPlan(size, total_bytes, requested_batch_size);
#ifdef NODE_RE2_PARALLEL
  if (plan.thread_count > 1) {
    std::atomic<bool> stopped{false};
    std::exception_ptr error;
    const auto run_batch = [&](size_t batch) noexcept {
      if (stopped.load(std::memory_order_relaxed)) {
        return;
      }
      try {
        const size_t begin = batch * plan.batch_size;
        const size_t end = begin + std::min(plan.batch_size, size - begin);
        for (size_t index = begin; index < end; ++index) {
          function(index);
        }
      } catch (...) {
        if (!stopped.exchange(true, std::memory_order_acq_rel)) {
          error = std::current_exception();
        }
      }
    };

    RunOnBatchThreadPool(plan.thread_count, plan.work_count, run_batch);
    if (error != nullptr) {
      std::rethrow_exception(error);
    }
    return;
  }
#else
  (void)plan;
#endif

  for (size_t index = 0; index < size; ++index) {
    function(index);
  }
}

}  // namespace node_re2
