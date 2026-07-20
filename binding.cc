#define NAPI_VERSION 10

#include <node_api.h>

#include <re2/re2.h>
#include <re2/set.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ByteView {
  const char* data;
  size_t size;
};

struct SetCompileWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  std::vector<std::string> patterns;
  std::unique_ptr<re2::RE2::Set> set;
  std::string error;
};

bool Check(napi_env env, napi_status status, const char* operation) {
  if (status == napi_ok) {
    return true;
  }
  if (status == napi_pending_exception) {
    return false;
  }

  const napi_extended_error_info* info = nullptr;
  const char* message = operation;
  if (napi_get_last_error_info(env, &info) == napi_ok && info != nullptr &&
      info->error_message != nullptr) {
    message = info->error_message;
  }
  napi_throw_error(env, nullptr, message);
  return false;
}

template <size_t N>
bool GetArguments(napi_env env, napi_callback_info info,
                  std::array<napi_value, N>* arguments) {
  size_t count = N;
  if (!Check(env,
             napi_get_cb_info(env, info, &count, arguments->data(), nullptr,
                              nullptr),
             "Failed to read arguments")) {
    return false;
  }
  if (count < N) {
    napi_throw_type_error(env, nullptr, "Not enough arguments");
    return false;
  }
  return true;
}

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

bool GetByteView(napi_env env, napi_value value, ByteView* view) {
  bool is_buffer = false;
  if (!Check(env, napi_is_buffer(env, value, &is_buffer),
             "Failed to inspect binary input")) {
    return false;
  }
  if (is_buffer) {
    void* data = nullptr;
    size_t size = 0;
    if (!Check(env, napi_get_buffer_info(env, value, &data, &size),
               "Failed to read Buffer")) {
      return false;
    }
    return AssignByteView(env, data, size, view);
  }

  bool is_typed_array = false;
  if (!Check(env, napi_is_typedarray(env, value, &is_typed_array),
             "Failed to inspect binary input")) {
    return false;
  }
  if (is_typed_array) {
    napi_typedarray_type type;
    size_t length = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env,
               napi_get_typedarray_info(env, value, &type, &length, &data,
                                        &array_buffer, &byte_offset),
               "Failed to read TypedArray")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    const size_t element_size = TypedArrayElementSize(type);
    if (element_size == 0 ||
        length > std::numeric_limits<size_t>::max() / element_size) {
      napi_throw_range_error(env, nullptr, "TypedArray is too large");
      return false;
    }
    const size_t size = length * element_size;
    return AssignByteView(env, data, size, view);
  }

  bool is_data_view = false;
  if (!Check(env, napi_is_dataview(env, value, &is_data_view),
             "Failed to inspect binary input")) {
    return false;
  }
  if (is_data_view) {
    size_t size = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env,
               napi_get_dataview_info(env, value, &size, &data, &array_buffer,
                                      &byte_offset),
               "Failed to read DataView")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    return AssignByteView(env, data, size, view);
  }

  napi_throw_type_error(env, nullptr,
                        "Expected a Buffer, TypedArray, or DataView");
  return false;
}

bool GetClampedIndex(napi_env env, napi_value value, size_t maximum,
                     size_t* result) {
  double number = 0;
  const napi_status status = napi_get_value_double(env, value, &number);
  if (status != napi_ok) {
    if (status == napi_pending_exception) {
      return false;
    }
    napi_throw_type_error(env, nullptr, "Byte ranges must be numbers");
    return false;
  }

  if (std::isnan(number)) {
    *result = 0;
  } else if (number <= 0) {
    *result = 0;
  } else if (number >= static_cast<double>(maximum)) {
    *result = maximum;
  } else {
    *result = static_cast<size_t>(std::trunc(number));
  }
  return true;
}

bool GetText(napi_env env, napi_value input, napi_value offset_value,
             napi_value length_value, std::string_view* text) {
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

bool GetPatterns(napi_env env, napi_value value,
                 std::vector<std::string>* patterns) {
  bool is_array = false;
  if (!Check(env, napi_is_array(env, value, &is_array),
             "Failed to inspect patterns")) {
    return false;
  }
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "patterns must be an array");
    return false;
  }

  uint32_t pattern_count = 0;
  if (!Check(env, napi_get_array_length(env, value, &pattern_count),
             "Failed to read patterns")) {
    return false;
  }
  if (pattern_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    napi_throw_range_error(env, nullptr, "Too many patterns");
    return false;
  }

  patterns->reserve(pattern_count);
  for (uint32_t index = 0; index < pattern_count; ++index) {
    napi_value pattern_value;
    if (!Check(env, napi_get_element(env, value, index, &pattern_value),
               "Failed to read pattern")) {
      return false;
    }
    ByteView pattern;
    if (!GetByteView(env, pattern_value, &pattern)) {
      return false;
    }
    patterns->emplace_back(pattern.data, pattern.size);
  }
  return true;
}

std::unique_ptr<re2::RE2::Set> CompileSet(
    const std::vector<std::string>& patterns, std::string* error) {
  re2::RE2::Options options;
  options.set_log_errors(false);
  auto set = std::make_unique<re2::RE2::Set>(options, re2::RE2::UNANCHORED);

  for (size_t index = 0; index < patterns.size(); ++index) {
    const int pattern_index = set->Add(patterns[index], error);
    if (pattern_index < 0) {
      return nullptr;
    }
    if (pattern_index != static_cast<int>(index)) {
      *error = "Unexpected RE2Set pattern index";
      return nullptr;
    }
  }

  if (!set->Compile()) {
    *error = "Failed to compile RE2Set";
    return nullptr;
  }
  return set;
}

template <typename T>
void Finalize(napi_env, void* data, void*) {
  delete static_cast<T*>(data);
}

template <typename T>
bool CreateExternal(napi_env env, std::unique_ptr<T> value,
                    napi_value* result) {
  if (!Check(env,
             napi_create_external(env, value.get(), Finalize<T>, nullptr,
                                  result),
             "Failed to create native context")) {
    return false;
  }
  value.release();
  return true;
}

napi_value RegexInit(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    ByteView pattern;
    if (!GetByteView(env, arguments[0], &pattern)) {
      return nullptr;
    }

    re2::RE2::Options options;
    options.set_log_errors(false);
    auto regex = std::make_unique<re2::RE2>(
        std::string_view(pattern.data, pattern.size), options);
    if (!regex->ok()) {
      napi_throw_error(env, nullptr, regex->error().c_str());
      return nullptr;
    }

    napi_value result;
    return CreateExternal(env, std::move(regex), &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

napi_value RegexTest(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    re2::RE2* regex = nullptr;
    if (!Check(env,
               napi_get_value_external(env, arguments[0],
                                       reinterpret_cast<void**>(&regex)),
               "Failed to read regex context")) {
      return nullptr;
    }

    std::string_view text;
    if (!GetText(env, arguments[1], arguments[2], arguments[3], &text)) {
      return nullptr;
    }

    napi_value result;
    if (!Check(env,
               napi_get_boolean(env, re2::RE2::PartialMatch(text, *regex),
                                &result),
               "Failed to create match result")) {
      return nullptr;
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

napi_value SetInit(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    bool is_array = false;
    if (!Check(env, napi_is_array(env, arguments[0], &is_array),
               "Failed to inspect patterns")) {
      return nullptr;
    }
    if (!is_array) {
      napi_throw_type_error(env, nullptr, "patterns must be an array");
      return nullptr;
    }

    uint32_t pattern_count = 0;
    if (!Check(env,
               napi_get_array_length(env, arguments[0], &pattern_count),
               "Failed to read patterns")) {
      return nullptr;
    }
    if (pattern_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      napi_throw_range_error(env, nullptr, "Too many patterns");
      return nullptr;
    }

    re2::RE2::Options options;
    options.set_log_errors(false);
    auto set = std::make_unique<re2::RE2::Set>(options, re2::RE2::UNANCHORED);

    for (uint32_t index = 0; index < pattern_count; ++index) {
      napi_value pattern_value;
      if (!Check(env,
                 napi_get_element(env, arguments[0], index, &pattern_value),
                 "Failed to read pattern")) {
        return nullptr;
      }
      ByteView pattern;
      if (!GetByteView(env, pattern_value, &pattern)) {
        return nullptr;
      }

      std::string error;
      const int pattern_index = set->Add(
          std::string_view(pattern.data, pattern.size), &error);
      if (pattern_index < 0) {
        napi_throw_error(env, nullptr, error.c_str());
        return nullptr;
      }
      if (pattern_index != static_cast<int>(index)) {
        napi_throw_error(env, nullptr, "Unexpected RE2Set pattern index");
        return nullptr;
      }
    }

    if (!set->Compile()) {
      napi_throw_error(env, nullptr, "Failed to compile RE2Set");
      return nullptr;
    }

    napi_value result;
    return CreateExternal(env, std::move(set), &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

void SetCompileExecute(napi_env, void* data) {
  auto* work = static_cast<SetCompileWork*>(data);
  try {
    work->set = CompileSet(work->patterns, &work->error);
  } catch (const std::exception& error) {
    work->error = error.what();
  } catch (...) {
    work->error = "Unknown RE2Set compilation failure";
  }
}

void RejectDeferred(napi_env env, napi_deferred deferred,
                    const std::string& message) {
  napi_value message_value;
  if (napi_create_string_utf8(env, message.c_str(), message.size(),
                              &message_value) != napi_ok) {
    return;
  }
  napi_value error;
  if (napi_create_error(env, nullptr, message_value, &error) != napi_ok) {
    return;
  }
  (void)napi_reject_deferred(env, deferred, error);
}

void SetCompileComplete(napi_env env, napi_status status, void* data) {
  std::unique_ptr<SetCompileWork> work(static_cast<SetCompileWork*>(data));
  (void)napi_delete_async_work(env, work->work);

  if (status != napi_ok) {
    RejectDeferred(env, work->deferred, "RE2Set compilation was cancelled");
    return;
  }
  if (work->set == nullptr) {
    RejectDeferred(
        env, work->deferred,
        work->error.empty() ? "Failed to compile RE2Set" : work->error);
    return;
  }

  napi_value context;
  if (napi_create_external(env, work->set.get(), Finalize<re2::RE2::Set>,
                           nullptr, &context) != napi_ok) {
    RejectDeferred(env, work->deferred,
                   "Failed to create native RE2Set context");
    return;
  }
  work->set.release();
  (void)napi_resolve_deferred(env, work->deferred, context);
}

napi_value SetCompileAsync(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    auto work = std::make_unique<SetCompileWork>();
    if (!GetPatterns(env, arguments[0], &work->patterns)) {
      return nullptr;
    }

    napi_value promise;
    if (!Check(env, napi_create_promise(env, &work->deferred, &promise),
               "Failed to create RE2Set compilation promise")) {
      return nullptr;
    }

    napi_value resource_name;
    if (!Check(env,
               napi_create_string_utf8(env, "@nxtedition/re2:compile-set",
                                       NAPI_AUTO_LENGTH, &resource_name),
               "Failed to create RE2Set async resource name") ||
        !Check(env,
               napi_create_async_work(env, nullptr, resource_name,
                                      SetCompileExecute, SetCompileComplete,
                                      work.get(), &work->work),
               "Failed to create RE2Set async work")) {
      return nullptr;
    }

    if (!Check(env, napi_queue_async_work(env, work->work),
               "Failed to queue RE2Set async work")) {
      (void)napi_delete_async_work(env, work->work);
      return nullptr;
    }

    work.release();
    return promise;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

napi_value SetTest(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    re2::RE2::Set* set = nullptr;
    if (!Check(env,
               napi_get_value_external(env, arguments[0],
                                       reinterpret_cast<void**>(&set)),
               "Failed to read set context")) {
      return nullptr;
    }

    std::string_view text;
    if (!GetText(env, arguments[1], arguments[2], arguments[3], &text)) {
      return nullptr;
    }

    std::vector<int> indices;
    re2::RE2::Set::ErrorInfo error_info{re2::RE2::Set::kNoError};
    const bool matched = set->Match(text, &indices, &error_info);
    if (!matched && error_info.kind != re2::RE2::Set::kNoError) {
      const char* message = "RE2Set matching failed";
      if (error_info.kind == re2::RE2::Set::kOutOfMemory) {
        message = "RE2Set matching failed: DFA out of memory";
      } else if (error_info.kind == re2::RE2::Set::kNotCompiled) {
        message = "RE2Set matching failed: set is not compiled";
      } else if (error_info.kind == re2::RE2::Set::kInconsistent) {
        message = "RE2Set matching failed: inconsistent result";
      }
      napi_throw_error(env, nullptr, message);
      return nullptr;
    }

    napi_value result;
    if (!Check(env,
               napi_create_array_with_length(env, indices.size(), &result),
               "Failed to create set result")) {
      return nullptr;
    }
    for (size_t index = 0; index < indices.size(); ++index) {
      napi_value element;
      if (!Check(env, napi_create_int32(env, indices[index], &element),
                 "Failed to create pattern index") ||
          !Check(env, napi_set_element(env, result, index, element),
                 "Failed to write pattern index")) {
        return nullptr;
      }
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

bool ExportFunction(napi_env env, napi_value exports, const char* name,
                    napi_callback callback) {
  napi_value function;
  return Check(env,
               napi_create_function(env, name, NAPI_AUTO_LENGTH, callback,
                                    nullptr, &function),
               "Failed to create native function") &&
         Check(env, napi_set_named_property(env, exports, name, function),
               "Failed to export native function");
}

}  // namespace

NAPI_MODULE_INIT() {
  if (!ExportFunction(env, exports, "regex_init", RegexInit) ||
      !ExportFunction(env, exports, "regex_test", RegexTest) ||
      !ExportFunction(env, exports, "set_init", SetInit) ||
      !ExportFunction(env, exports, "set_compile_async", SetCompileAsync) ||
      !ExportFunction(env, exports, "set_test", SetTest)) {
    return nullptr;
  }
  return exports;
}
