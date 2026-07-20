#define NAPI_VERSION 10

#include <node_api.h>

#include "native/addon-lifecycle.h"
#include "native/batch-binding.h"
#include "native/regex-binding.h"
#include "native/set-binding.h"

namespace node_re2 {

napi_value Initialize(napi_env env, napi_value exports) {
  if (!RegisterAddonEnvironment(env) || !RegisterRegexBindings(env, exports) || !RegisterSetBindings(env, exports) ||
      !RegisterBatchBindings(env, exports)) {
    return nullptr;
  }
  return exports;
}

}  // namespace node_re2

NAPI_MODULE_INIT() {
  return node_re2::Initialize(env, exports);
}
