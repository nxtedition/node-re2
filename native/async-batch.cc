#include "async-batch.h"

#include "napi-utils.h"

namespace node_re2 {

bool GetAsyncBatch(napi_env env, napi_value inputs, bool unsafe, AsyncBatch* batch) {
  batch->unsafe = unsafe;
  if (!unsafe) {
    if (!GetTextBatch(env, inputs, &batch->snapshot)) {
      return false;
    }
    batch->total_bytes = batch->snapshot.bytes.size();
    return true;
  }

  const napi_status reference_status = napi_create_reference(env, inputs, 1, &batch->borrowed_inputs);
  if (reference_status != napi_ok) {
    return Check(env, reference_status, "Failed to retain unsafe batch inputs");
  }

  try {
    if (GetTexts(env, inputs, &batch->borrowed_texts, &batch->total_bytes)) {
      return true;
    }
  } catch (...) {
    (void)ReleaseAsyncBatch(env, batch);
    throw;
  }
  (void)ReleaseAsyncBatch(env, batch);
  return false;
}

napi_status ReleaseAsyncBatch(napi_env env, AsyncBatch* batch) {
  if (batch->borrowed_inputs == nullptr) {
    return napi_ok;
  }
  const napi_ref reference = batch->borrowed_inputs;
  batch->borrowed_inputs = nullptr;
  return napi_delete_reference(env, reference);
}

}  // namespace node_re2
