#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 10
#endif

#include <node_api.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace node_re2 {

struct ByteView {
  const char* data;
  size_t size;
};

inline constexpr size_t kMaxPatternBytes = 16 << 20;

bool Check(napi_env env, napi_status status, const char* operation);
bool ExportFunction(napi_env env, napi_value exports, const char* name, napi_callback callback);

template <size_t N>
bool GetArguments(napi_env env, napi_callback_info info, std::array<napi_value, N>* arguments) {
  size_t count = N;
  if (!Check(env, napi_get_cb_info(env, info, &count, arguments->data(), nullptr, nullptr),
             "Failed to read arguments")) {
    return false;
  }
  if (count < N) {
    napi_throw_type_error(env, nullptr, "Not enough arguments");
    return false;
  }
  return true;
}

template <size_t Required, size_t N>
bool GetArgumentsWithOptional(napi_env env, napi_callback_info info, std::array<napi_value, N>* arguments) {
  static_assert(Required <= N);
  size_t count = N;
  if (!Check(env, napi_get_cb_info(env, info, &count, arguments->data(), nullptr, nullptr),
             "Failed to read arguments")) {
    return false;
  }
  if (count < Required) {
    napi_throw_type_error(env, nullptr, "Not enough arguments");
    return false;
  }
  for (size_t index = count; index < N; ++index) {
    if (!Check(env, napi_get_undefined(env, &(*arguments)[index]), "Failed to create optional argument")) {
      return false;
    }
  }
  return true;
}

bool GetByteView(napi_env env, napi_value value, ByteView* view);
bool GetPattern(napi_env env, napi_value value, std::string* pattern, size_t maximum = kMaxPatternBytes);
bool GetText(napi_env env, napi_value input, napi_value offset_value, napi_value length_value, std::string_view* text);
bool GetPatterns(napi_env env, napi_value value, std::vector<std::string>* patterns);
bool GetTexts(napi_env env, napi_value value, std::vector<std::string_view>* texts, size_t* total_bytes);
bool GetBatchSize(napi_env env, napi_value value, size_t* batch_size);

template <typename T>
void Finalize(napi_env, void* data, void*) {
  delete static_cast<T*>(data);
}

template <typename T>
napi_status CreateTaggedExternalRaw(napi_env env,
                                    std::unique_ptr<T> value,
                                    const napi_type_tag* type_tag,
                                    napi_value* result) {
  const napi_status create_status = napi_create_external(env, value.get(), Finalize<T>, nullptr, result);
  if (create_status != napi_ok) {
    return create_status;
  }
  value.release();
  return napi_type_tag_object(env, *result, type_tag);
}

template <typename T>
bool CreateTaggedExternal(napi_env env, std::unique_ptr<T> value, const napi_type_tag* type_tag, napi_value* result) {
  return Check(env, CreateTaggedExternalRaw(env, std::move(value), type_tag, result),
               "Failed to create native context");
}

template <typename T>
bool GetTaggedExternal(napi_env env,
                       napi_value value,
                       const napi_type_tag* type_tag,
                       const char* error_message,
                       T** result) {
  bool matches = false;
  const napi_status tag_status = napi_check_object_type_tag(env, value, type_tag, &matches);
  if (tag_status == napi_pending_exception) {
    return false;
  }
  if (tag_status != napi_ok || !matches) {
    napi_throw_type_error(env, nullptr, error_message);
    return false;
  }
  return Check(env, napi_get_value_external(env, value, reinterpret_cast<void**>(result)),
               "Failed to read native context");
}

}  // namespace node_re2
