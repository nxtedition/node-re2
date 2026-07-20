#pragma once

#include <uv.h>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace node_re2 {

#ifdef NODE_RE2_BATCH_BYTES_PER_THREAD
inline constexpr size_t kBatchBytesPerThread = NODE_RE2_BATCH_BYTES_PER_THREAD;
#else
inline constexpr size_t kBatchBytesPerThread = 128 << 10;
#endif
static_assert(kBatchBytesPerThread > 0);

#ifdef NODE_RE2_BATCHES_PER_THREAD
inline constexpr size_t kBatchesPerThread = NODE_RE2_BATCHES_PER_THREAD;
#else
inline constexpr size_t kBatchesPerThread = 16;
#endif
static_assert(kBatchesPerThread > 0);

struct BatchPlan {
  size_t thread_count;
  size_t batch_size;
  size_t work_count;
};

inline size_t MaxBatchParallelism() {
#ifdef NODE_RE2_PARALLEL
  static const size_t parallelism = [] {
    const size_t available = std::max<unsigned int>(uv_available_parallelism(), 1);
    return std::max<size_t>(available / 2, 1);
  }();
  return parallelism;
#else
  return 1;
#endif
}

inline BatchPlan MakeBatchPlan(size_t size, size_t total_bytes, size_t requested_batch_size) {
  if (size == 0) {
    return {};
  }
#ifdef NODE_RE2_PARALLEL
  const size_t work_threads = total_bytes == 0 ? 1 : 1 + ((total_bytes - 1) / kBatchBytesPerThread);
  const size_t maximum_threads = std::min({MaxBatchParallelism(), size, work_threads});
  size_t batch_size = requested_batch_size;
  if (batch_size == 0) {
    const size_t requested_batches = maximum_threads > std::numeric_limits<size_t>::max() / kBatchesPerThread
                                         ? size
                                         : maximum_threads * kBatchesPerThread;
    const size_t batch_count = std::min(size, requested_batches);
    batch_size = 1 + ((size - 1) / batch_count);
  } else {
    batch_size = std::min(batch_size, size);
  }
  const size_t work_count = 1 + ((size - 1) / batch_size);
  return {
      .thread_count = std::min(maximum_threads, work_count),
      .batch_size = batch_size,
      .work_count = work_count,
  };
#else
  (void)total_bytes;
  (void)requested_batch_size;
  return {.thread_count = 1, .batch_size = size, .work_count = 1};
#endif
}

inline size_t BatchParallelism(size_t size, size_t total_bytes, size_t batch_size) {
  return MakeBatchPlan(size, total_bytes, batch_size).thread_count;
}

}  // namespace node_re2
