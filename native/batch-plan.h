#pragma once

#include <uv.h>

#include <algorithm>
#include <cstddef>

namespace node_re2 {

#ifdef NODE_RE2_BATCH_BYTES_PER_THREAD
inline constexpr size_t kBatchBytesPerThread = NODE_RE2_BATCH_BYTES_PER_THREAD;
#else
inline constexpr size_t kBatchBytesPerThread = 128 << 10;
#endif
static_assert(kBatchBytesPerThread > 0);

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

inline size_t BatchParallelism(size_t size, size_t total_bytes) {
  if (size == 0) {
    return 0;
  }
#ifdef NODE_RE2_PARALLEL
  const size_t work_threads =
      total_bytes == 0 ? 1 : 1 + ((total_bytes - 1) / kBatchBytesPerThread);
  return std::min({MaxBatchParallelism(), size, work_threads});
#else
  (void)total_bytes;
  return 1;
#endif
}

}  // namespace node_re2
