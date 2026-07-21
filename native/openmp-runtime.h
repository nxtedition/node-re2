#pragma once

#include <cstddef>

#ifdef NODE_RE2_OPENMP
#include <omp.h>

#include <mutex>
#endif

#include "batch-plan.h"

namespace node_re2 {

#ifdef NODE_RE2_OPENMP
inline std::mutex& OpenMpSubmissionMutex() {
  static std::mutex mutex;
  return mutex;
}
#endif

inline size_t ObserveBatchParallelism(const BatchPlan& plan) {
#ifdef NODE_RE2_OPENMP
  if (plan.thread_count > 1) {
    std::lock_guard submission_lock(OpenMpSubmissionMutex());
    size_t observed = 1;

#pragma omp parallel num_threads(plan.thread_count)
    {
#pragma omp single
      observed = static_cast<size_t>(omp_get_num_threads());
    }
    return observed;
  }
#endif
  return plan.thread_count;
}

}  // namespace node_re2
