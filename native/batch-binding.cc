#include "batch-binding.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "batch-plan.h"
#include "napi-utils.h"

namespace node_re2 {
namespace {

bool GetSizeArgument(napi_env env, napi_value value, const char* name, size_t* result) {
  constexpr double kMaxSafeInteger = 9007199254740991.0;
  double number = 0;
  const napi_status status = napi_get_value_double(env, value, &number);
  if (status != napi_ok) {
    if (status == napi_pending_exception) {
      return false;
    }
    napi_throw_type_error(env, nullptr, name);
    return false;
  }
  if (!std::isfinite(number) || number < 0 || std::trunc(number) != number || number > kMaxSafeInteger) {
    napi_throw_range_error(env, nullptr, name);
    return false;
  }
  *result = static_cast<size_t>(number);
  return true;
}

napi_value BatchThreadCount(napi_env env, napi_callback_info info) {
  std::array<napi_value, 3> arguments;
  if (!GetArgumentsWithOptional<2>(env, info, &arguments)) {
    return nullptr;
  }
  size_t size = 0;
  size_t total_bytes = 0;
  if (!GetSizeArgument(env, arguments[0], "Batch size must be a non-negative integer", &size) ||
      !GetSizeArgument(env, arguments[1], "Batch bytes must be a non-negative integer", &total_bytes)) {
    return nullptr;
  }
  size_t batch_size = 0;
  if (!GetBatchSize(env, arguments[2], &batch_size)) {
    return nullptr;
  }

  napi_value result;
  return Check(env, napi_create_uint32(env, BatchParallelism(size, total_bytes, batch_size), &result),
               "Failed to create batch thread count")
             ? result
             : nullptr;
}

}  // namespace

bool RegisterBatchBindings(napi_env env, napi_value exports) {
  return ExportFunction(env, exports, "batch_parallelism", BatchThreadCount);
}

}  // namespace node_re2
