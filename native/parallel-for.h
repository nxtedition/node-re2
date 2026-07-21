#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>

#include "batch-plan.h"
#include "openmp-runtime.h"

namespace node_re2 {

template <typename Function>
void ParallelFor(size_t size, size_t total_bytes, size_t requested_batch_size, Function&& function) {
  const BatchPlan plan = MakeBatchPlan(size, total_bytes, requested_batch_size);
#ifdef NODE_RE2_OPENMP
  if (plan.thread_count > 1) {
    std::lock_guard submission_lock(OpenMpSubmissionMutex());
    std::atomic<bool> stopped{false};
    std::mutex error_mutex;
    std::exception_ptr error;
    const std::ptrdiff_t work_count = static_cast<std::ptrdiff_t>(plan.work_count);

#pragma omp parallel for schedule(dynamic, 1) num_threads(plan.thread_count)
    for (std::ptrdiff_t batch = 0; batch < work_count; ++batch) {
      if (stopped.load(std::memory_order_relaxed)) {
        continue;
      }
      try {
        const size_t begin = static_cast<size_t>(batch) * plan.batch_size;
        const size_t end = begin + std::min(plan.batch_size, size - begin);
        for (size_t index = begin; index < end; ++index) {
          function(index);
        }
      } catch (...) {
        if (!stopped.exchange(true, std::memory_order_acq_rel)) {
          std::lock_guard error_lock(error_mutex);
          error = std::current_exception();
        }
      }
    }

    {
      std::lock_guard error_lock(error_mutex);
      if (error != nullptr) {
        std::rethrow_exception(error);
      }
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
