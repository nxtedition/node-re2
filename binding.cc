#define NAPI_VERSION 8

#include <napi-macros.h>
#include <node_api.h>

#include <re2/re2.h>

NAPI_METHOD(regex_init) {
  NAPI_ARGV(2);

  return 0;
}

NAPI_INIT() {
  NAPI_EXPORT_FUNCTION(regex_init);
}
