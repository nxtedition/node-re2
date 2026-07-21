#include "napi-utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "text-batch.h"

namespace node_re2 {
namespace {

constexpr uint32_t kMaxBatchInputCount = 1 << 20;
constexpr uint32_t kMaxSetPatternCount = 100'000;

bool AssignByteView(napi_env env, void* data, size_t size, ByteView* view) {
  if (size != 0 && data == nullptr) {
    napi_throw_error(env, nullptr, "Binary input has no data");
    return false;
  }
  *view = {size == 0 ? "" : static_cast<const char*>(data), size};
  return true;
}

size_t TypedArrayElementSize(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array:
    case napi_uint8_array:
    case napi_uint8_clamped_array:
      return 1;
    case napi_int16_array:
    case napi_uint16_array:
#ifdef NODE_API_HAS_FLOAT16_ARRAY
    case napi_float16_array:
#endif
      return 2;
    case napi_int32_array:
    case napi_uint32_array:
    case napi_float32_array:
      return 4;
    case napi_float64_array:
    case napi_bigint64_array:
    case napi_biguint64_array:
      return 8;
  }
  return 0;
}

bool GetClampedIndex(napi_env env, napi_value value, size_t maximum, size_t* result) {
  double number = 0;
  const napi_status status = napi_get_value_double(env, value, &number);
  if (status != napi_ok) {
    if (status == napi_pending_exception) {
      return false;
    }
    napi_throw_type_error(env, nullptr, "Byte ranges must be numbers");
    return false;
  }

  if (std::isnan(number) || number <= 0) {
    *result = 0;
  } else if (number >= static_cast<double>(maximum)) {
    *result = maximum;
  } else {
    *result = static_cast<size_t>(std::trunc(number));
  }
  return true;
}

bool GetString(napi_env env, napi_value value, size_t maximum, std::string* result) {
  size_t size = 0;
  if (!Check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &size), "Failed to read pattern")) {
    return false;
  }
  if (size > maximum) {
    napi_throw_range_error(env, nullptr, "Pattern data is too large");
    return false;
  }

  result->resize(size + 1);
  size_t written = 0;
  if (!Check(env, napi_get_value_string_utf8(env, value, result->data(), result->size(), &written),
             "Failed to read pattern")) {
    return false;
  }
  result->resize(written);
  return true;
}

bool GetPatternImpl(napi_env env, napi_value value, size_t maximum, std::string* result) {
  napi_valuetype type;
  if (!Check(env, napi_typeof(env, value, &type), "Failed to inspect pattern")) {
    return false;
  }
  if (type == napi_string) {
    return GetString(env, value, maximum, result);
  }

  ByteView view;
  if (!GetByteView(env, value, &view)) {
    return false;
  }
  if (view.size > maximum) {
    napi_throw_range_error(env, nullptr, "Pattern data is too large");
    return false;
  }
  result->assign(view.data, view.size);
  return true;
}

bool TakePendingException(napi_env env, napi_value* exception) {
  bool pending = false;
  return napi_is_exception_pending(env, &pending) == napi_ok && pending &&
         napi_get_and_clear_last_exception(env, exception) == napi_ok;
}

bool GetInputValues(napi_env env, napi_value value, std::vector<napi_value>* inputs) {
  bool is_array = false;
  if (!Check(env, napi_is_array(env, value, &is_array), "Failed to inspect inputs")) {
    return false;
  }
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "inputs must be an array");
    return false;
  }

  uint32_t input_count = 0;
  if (!Check(env, napi_get_array_length(env, value, &input_count), "Failed to read inputs")) {
    return false;
  }
  if (input_count > kMaxBatchInputCount) {
    napi_throw_range_error(env, nullptr, "Too many inputs");
    return false;
  }

  inputs->reserve(input_count);
  for (uint32_t index = 0; index < input_count; ++index) {
    napi_value input;
    if (!Check(env, napi_get_element(env, value, index, &input), "Failed to read input")) {
      return false;
    }
    inputs->push_back(input);
  }
  return true;
}

}  // namespace

bool Check(napi_env env, napi_status status, const char* operation) {
  if (status == napi_ok) {
    return true;
  }
  if (status == napi_pending_exception) {
    return false;
  }

  const napi_extended_error_info* info = nullptr;
  const char* message = operation;
  if (napi_get_last_error_info(env, &info) == napi_ok && info != nullptr && info->error_message != nullptr) {
    message = info->error_message;
  }
  napi_throw_error(env, nullptr, message);
  return false;
}

bool ExportFunction(napi_env env, napi_value exports, const char* name, napi_callback callback) {
  napi_value function;
  return Check(env, napi_create_function(env, name, NAPI_AUTO_LENGTH, callback, nullptr, &function),
               "Failed to create native function") &&
         Check(env, napi_set_named_property(env, exports, name, function), "Failed to export native function");
}

bool GetByteView(napi_env env, napi_value value, ByteView* view) {
  bool is_buffer = false;
  if (!Check(env, napi_is_buffer(env, value, &is_buffer), "Failed to inspect binary input")) {
    return false;
  }
  if (is_buffer) {
    void* data = nullptr;
    size_t size = 0;
    if (!Check(env, napi_get_buffer_info(env, value, &data, &size), "Failed to read Buffer")) {
      return false;
    }
    return AssignByteView(env, data, size, view);
  }

  bool is_typed_array = false;
  if (!Check(env, napi_is_typedarray(env, value, &is_typed_array), "Failed to inspect binary input")) {
    return false;
  }
  if (is_typed_array) {
    napi_typedarray_type type;
    size_t length = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env, napi_get_typedarray_info(env, value, &type, &length, &data, &array_buffer, &byte_offset),
               "Failed to read TypedArray")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    const size_t element_size = TypedArrayElementSize(type);
    if (element_size == 0 || length > std::numeric_limits<size_t>::max() / element_size) {
      napi_throw_range_error(env, nullptr, "TypedArray is too large");
      return false;
    }
    return AssignByteView(env, data, length * element_size, view);
  }

  bool is_data_view = false;
  if (!Check(env, napi_is_dataview(env, value, &is_data_view), "Failed to inspect binary input")) {
    return false;
  }
  if (is_data_view) {
    size_t size = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env, napi_get_dataview_info(env, value, &size, &data, &array_buffer, &byte_offset),
               "Failed to read DataView")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    return AssignByteView(env, data, size, view);
  }

  napi_throw_type_error(env, nullptr, "Expected a Buffer, TypedArray, or DataView");
  return false;
}

bool GetPattern(napi_env env, napi_value value, std::string* pattern, size_t maximum) {
  return GetPatternImpl(env, value, maximum, pattern);
}

bool GetText(napi_env env, napi_value input, napi_value offset_value, napi_value length_value, std::string_view* text) {
  ByteView view;
  if (!GetByteView(env, input, &view)) {
    return false;
  }

  size_t offset = 0;
  if (!GetClampedIndex(env, offset_value, view.size, &offset)) {
    return false;
  }
  size_t length = 0;
  if (!GetClampedIndex(env, length_value, view.size - offset, &length)) {
    return false;
  }

  *text = std::string_view(view.data + offset, length);
  return true;
}

bool GetPatterns(napi_env env, napi_value value, std::vector<std::string>* patterns) {
  bool is_array = false;
  if (!Check(env, napi_is_array(env, value, &is_array), "Failed to inspect patterns")) {
    return false;
  }
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "patterns must be an array");
    return false;
  }

  uint32_t pattern_count = 0;
  if (!Check(env, napi_get_array_length(env, value, &pattern_count), "Failed to read patterns")) {
    return false;
  }
  if (pattern_count > kMaxSetPatternCount) {
    napi_throw_range_error(env, nullptr, "Too many patterns");
    return false;
  }

  patterns->reserve(pattern_count);
  size_t total_bytes = 0;
  for (uint32_t index = 0; index < pattern_count; ++index) {
    napi_value pattern_value;
    if (!Check(env, napi_get_element(env, value, index, &pattern_value), "Failed to read pattern")) {
      return false;
    }
    std::string pattern;
    if (!GetPattern(env, pattern_value, &pattern, kMaxPatternBytes - total_bytes)) {
      return false;
    }
    total_bytes += pattern.size();
    patterns->push_back(std::move(pattern));
  }
  return true;
}

bool GetTexts(napi_env env, napi_value value, std::vector<std::string_view>* texts, size_t* total_bytes) {
  std::vector<napi_value> inputs;
  if (!GetInputValues(env, value, &inputs)) {
    return false;
  }

  texts->reserve(inputs.size());
  *total_bytes = 0;
  for (napi_value input : inputs) {
    ByteView view;
    if (!GetByteView(env, input, &view)) {
      return false;
    }
    if (view.size > std::numeric_limits<size_t>::max() - *total_bytes) {
      napi_throw_range_error(env, nullptr, "Batch input data is too large");
      return false;
    }
    *total_bytes += view.size;
    texts->emplace_back(view.data, view.size);
  }
  return true;
}

bool GetTextBatch(napi_env env, napi_value value, TextBatch* texts) {
  texts->bytes.clear();
  texts->offsets.clear();

  std::vector<napi_value> inputs;
  if (!GetInputValues(env, value, &inputs)) {
    return false;
  }

  std::vector<ByteView> views;
  views.reserve(inputs.size());
  size_t total_bytes = 0;
  for (napi_value input : inputs) {
    ByteView view;
    if (!GetByteView(env, input, &view)) {
      return false;
    }
    if (view.size > std::numeric_limits<size_t>::max() - total_bytes) {
      napi_throw_range_error(env, nullptr, "Batch input data is too large");
      return false;
    }
    total_bytes += view.size;
    views.push_back(view);
  }

  texts->bytes.reserve(total_bytes);
  texts->offsets.reserve(views.size() + 1);
  texts->offsets.push_back(0);
  for (const ByteView view : views) {
    texts->bytes.append(view.data, view.size);
    texts->offsets.push_back(texts->bytes.size());
  }
  return true;
}

bool GetBatchSize(napi_env env, napi_value value, size_t* batch_size) {
  napi_valuetype type;
  if (!Check(env, napi_typeof(env, value, &type), "Failed to inspect batchSize")) {
    return false;
  }
  if (type == napi_undefined) {
    *batch_size = 0;
    return true;
  }
  if (type != napi_number) {
    napi_throw_type_error(env, nullptr, "batchSize must be a number");
    return false;
  }

  double number = 0;
  const napi_status status = napi_get_value_double(env, value, &number);
  if (status != napi_ok) {
    if (status != napi_pending_exception) {
      napi_throw_range_error(env, nullptr, "batchSize must be a positive safe integer or Infinity");
    }
    return false;
  }

  if (number == std::numeric_limits<double>::infinity()) {
    *batch_size = std::numeric_limits<size_t>::max();
    return true;
  }
  constexpr double kMaxSafeInteger = 9007199254740991.0;
  if (!std::isfinite(number) || number < 0 || std::trunc(number) != number || number > kMaxSafeInteger ||
      number > static_cast<double>(std::numeric_limits<size_t>::max())) {
    napi_throw_range_error(env, nullptr, "batchSize must be a positive safe integer or Infinity");
    return false;
  }
  *batch_size = static_cast<size_t>(number);
  return true;
}

bool GetBoolean(napi_env env, napi_value value, const char* error_message, bool* result) {
  napi_valuetype type;
  if (!Check(env, napi_typeof(env, value, &type), "Failed to inspect boolean option")) {
    return false;
  }
  if (type == napi_undefined) {
    *result = false;
    return true;
  }
  if (type != napi_boolean) {
    napi_throw_type_error(env, nullptr, error_message);
    return false;
  }
  return Check(env, napi_get_value_bool(env, value, result), "Failed to read boolean option");
}

void RejectDeferred(napi_env env, napi_deferred deferred, std::string_view message) {
  napi_value reason;
  if (TakePendingException(env, &reason)) {
    (void)napi_reject_deferred(env, deferred, reason);
    return;
  }

  napi_value message_value;
  if (napi_create_string_utf8(env, message.data(), message.size(), &message_value) != napi_ok ||
      napi_create_error(env, nullptr, message_value, &reason) != napi_ok) {
    if (!TakePendingException(env, &reason) && napi_get_undefined(env, &reason) != napi_ok) {
      return;
    }
  }
  (void)napi_reject_deferred(env, deferred, reason);
}

}  // namespace node_re2
