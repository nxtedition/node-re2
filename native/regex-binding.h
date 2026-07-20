#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 10
#endif

#include <node_api.h>

namespace node_re2 {

bool RegisterRegexBindings(napi_env env, napi_value exports);

}  // namespace node_re2
