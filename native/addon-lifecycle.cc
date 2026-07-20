#include "addon-lifecycle.h"

#include <memory>
#include <new>

#include "napi-utils.h"
#include "thread-pool.h"

namespace node_re2 {
namespace {

struct EnvironmentRegistration {};

void CleanupAddonEnvironment(void* data) {
  std::unique_ptr<EnvironmentRegistration> registration(static_cast<EnvironmentRegistration*>(data));
  ReleaseBatchThreadPoolEnvironment();
}

}  // namespace

bool RegisterAddonEnvironment(napi_env env) {
  auto registration = std::unique_ptr<EnvironmentRegistration>(new (std::nothrow) EnvironmentRegistration());
  if (registration == nullptr) {
    napi_throw_error(env, nullptr, "Failed to allocate native addon registration");
    return false;
  }
  if (!RetainBatchThreadPoolEnvironment()) {
    napi_throw_error(env, nullptr, "Failed to register native addon environment");
    return false;
  }

  const napi_status status = napi_add_env_cleanup_hook(env, CleanupAddonEnvironment, registration.get());
  if (status == napi_ok) {
    registration.release();
    return true;
  }

  ReleaseBatchThreadPoolEnvironment();
  return Check(env, status, "Failed to register native addon cleanup");
}

}  // namespace node_re2
