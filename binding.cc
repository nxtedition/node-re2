#define NAPI_VERSION 8

#include <napi-macros.h>
#include <node_api.h>

#include <re2/re2.h>

#include <memory>

template <typename T>
static void Finalize(napi_env env, void* data, void* hint) {
  if (hint) {
    delete reinterpret_cast<T*>(hint);
  }
}

NAPI_METHOD(regex_init) {
  NAPI_ARGV(1);

  std::string_view pattern;
  {
    char* buf = nullptr;
    size_t length = 0;
    NAPI_STATUS_THROWS(napi_get_buffer_info(env, argv[0], reinterpret_cast<void**>(&buf), &length));
    pattern = std::string_view(buf, length);
  }

  auto regex = std::make_unique<re2::RE2>(pattern);

  napi_value result;
  NAPI_STATUS_THROWS(napi_create_external(env, regex.get(), Finalize<re2::RE2>, regex.get(), &result));
  regex.release();

  return result;
}

NAPI_METHOD(regex_test) {
  NAPI_ARGV(2);

  re2::RE2* regex;
  NAPI_STATUS_THROWS(napi_get_value_external(env, argv[0], reinterpret_cast<void**>(&regex)));

  std::string_view value;
  {
    char* buf = nullptr;
    size_t length = 0;
    NAPI_STATUS_THROWS(napi_get_buffer_info(env, argv[1], reinterpret_cast<void**>(&buf), &length));
    value = std::string_view(buf, length);
  }

  napi_value result;
  NAPI_STATUS_THROWS(napi_get_boolean(env, re2::RE2::PartialMatch(value, *regex), &result));
  return result;
}

NAPI_INIT() {
  NAPI_EXPORT_FUNCTION(regex_init);
  NAPI_EXPORT_FUNCTION(regex_test);
}
