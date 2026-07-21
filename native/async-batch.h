#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 10
#endif

#include <node_api.h>

#include <cstddef>
#include <string_view>
#include <vector>

#include "text-batch.h"

namespace node_re2 {

struct AsyncBatch {
  napi_ref borrowed_inputs = nullptr;
  TextBatch snapshot;
  std::vector<std::string_view> borrowed_texts;
  size_t total_bytes = 0;
  bool unsafe = false;

  [[nodiscard]] size_t size() const { return unsafe ? borrowed_texts.size() : snapshot.size(); }

  [[nodiscard]] std::string_view operator[](size_t index) const {
    return unsafe ? borrowed_texts[index] : snapshot[index];
  }
};

bool GetAsyncBatch(napi_env env, napi_value inputs, bool unsafe, AsyncBatch* batch);
napi_status ReleaseAsyncBatch(napi_env env, AsyncBatch* batch);

}  // namespace node_re2
