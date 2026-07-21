#include "set-binding.h"

#include <re2/set.h>

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async-batch.h"
#include "napi-utils.h"
#include "parallel-for.h"
#include "set-cache.h"

namespace node_re2 {
namespace {

constexpr napi_type_tag kSetContextTypeTag{UINT64_C(0xa84ea42fab944775), UINT64_C(0xb4cd9a935ec36f8e)};

struct SetCompileWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  PendingSetCompilation pending;
  SharedSet set;
  bool owner = false;
  bool unexpected_failure = false;
};

struct SetMatchWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  SharedSet set;
  AsyncBatch inputs;
  size_t batch_size = 0;
  std::vector<std::vector<int>> matches;
  std::vector<re2::RE2::Set::ErrorKind> errors;
  re2::RE2::Set::ErrorKind match_error = re2::RE2::Set::kNoError;
  bool failed = false;
};

napi_status CreateSetExternalRaw(napi_env env, SharedSet set, napi_value* result) {
  return CreateTaggedExternalRaw(env, std::make_unique<SetContext>(SetContext{std::move(set)}), &kSetContextTypeTag,
                                 result);
}

bool CreateSetExternal(napi_env env, SharedSet set, napi_value* result) {
  return Check(env, CreateSetExternalRaw(env, std::move(set), result), "Failed to create native RE2Set context");
}

napi_value SetInit(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    std::vector<std::string> patterns;
    if (!GetPatterns(env, arguments[0], &patterns)) {
      return nullptr;
    }

    std::string error;
    SharedSet set = CompileSet(patterns, &error);
    if (set == nullptr) {
      napi_throw_error(env, nullptr, error.empty() ? "Failed to compile RE2Set" : error.c_str());
      return nullptr;
    }
    napi_value result;
    return CreateSetExternal(env, std::move(set), &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

void SetCompileExecute(napi_env, void* data) noexcept {
  auto* work = static_cast<SetCompileWork*>(data);
  try {
    if (work->owner) {
      work->set = RunSetCompilation(work->pending, &work->unexpected_failure);
    } else {
      work->set = WaitForSetCompilation(work->pending);
      work->unexpected_failure = SetCompilationFailedUnexpectedly(work->pending);
    }
  } catch (...) {
    work->set.reset();
    work->unexpected_failure = true;
  }
}

void SetCompileComplete(napi_env env, napi_status status, void* data) {
  std::unique_ptr<SetCompileWork> work(static_cast<SetCompileWork*>(data));
  (void)napi_delete_async_work(env, work->work);

  if (status != napi_ok) {
    if (work->owner) {
      CancelSetCompilation(work->pending);
    }
    RejectDeferred(env, work->deferred, "RE2Set compilation was cancelled");
    return;
  }

  if (work->set == nullptr) {
    const bool unexpected = work->unexpected_failure || SetCompilationFailedUnexpectedly(work->pending);
    const std::string_view error = SetCompilationError(work->pending);
    if (work->owner) {
      ReleaseSetCompilation(work->pending);
    }
    RejectDeferred(env, work->deferred, unexpected || error.empty() ? "Failed to compile RE2Set" : error);
    return;
  }

  if (work->owner) {
    ReleaseSetCompilation(work->pending);
  }

  napi_value context;
  try {
    if (CreateSetExternalRaw(env, std::move(work->set), &context) != napi_ok) {
      RejectDeferred(env, work->deferred, "Failed to create native RE2Set context");
      return;
    }
  } catch (const std::exception& error) {
    RejectDeferred(env, work->deferred, error.what());
    return;
  } catch (...) {
    RejectDeferred(env, work->deferred, "Failed to create native RE2Set context");
    return;
  }
  if (napi_resolve_deferred(env, work->deferred, context) != napi_ok) {
    RejectDeferred(env, work->deferred, "Failed to resolve RE2Set compilation");
  }
}

bool ResolveCompiledSet(napi_env env, napi_deferred deferred, SharedSet set, std::string_view failure_message) {
  napi_value context;
  if (CreateSetExternalRaw(env, std::move(set), &context) != napi_ok) {
    RejectDeferred(env, deferred, "Failed to create native RE2Set context");
    return false;
  }
  if (napi_resolve_deferred(env, deferred, context) != napi_ok) {
    RejectDeferred(env, deferred, failure_message);
    return false;
  }
  return true;
}

napi_value SetCompileAsync(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  napi_deferred deferred;
  napi_value promise;
  if (!Check(env, napi_create_promise(env, &deferred, &promise), "Failed to create RE2Set compilation promise")) {
    return nullptr;
  }

  std::unique_ptr<SetCompileWork> work;
  try {
    std::string cache_key;
    {
      std::vector<std::string> patterns;
      if (!GetPatterns(env, arguments[0], &patterns)) {
        RejectDeferred(env, deferred, "Failed to read RE2Set patterns");
        return promise;
      }
      cache_key = SetCompileCacheKey(patterns);
    }

    if (SharedSet cached_set = FindCachedSet(cache_key)) {
      ResolveCompiledSet(env, deferred, std::move(cached_set), "Failed to resolve cached RE2Set compilation");
      return promise;
    }

    work = std::make_unique<SetCompileWork>();
    work->deferred = deferred;
    napi_value resource_name;
    if (napi_create_string_utf8(env, "@nxtedition/re2:compile-set", NAPI_AUTO_LENGTH, &resource_name) != napi_ok) {
      RejectDeferred(env, deferred, "Failed to create RE2Set async resource name");
      return promise;
    }
    if (napi_create_async_work(env, nullptr, resource_name, SetCompileExecute, SetCompileComplete, work.get(),
                               &work->work) != napi_ok) {
      RejectDeferred(env, deferred, "Failed to create RE2Set async work");
      return promise;
    }

    SetCompilationTicket ticket = AcquireSetCompilation(std::move(cache_key));
    if (ticket.cached_set != nullptr) {
      (void)napi_delete_async_work(env, work->work);
      work->work = nullptr;
      ResolveCompiledSet(env, deferred, std::move(ticket.cached_set), "Failed to resolve cached RE2Set compilation");
      return promise;
    }
    work->pending = std::move(ticket.pending);
    work->owner = ticket.owner;

    if (napi_queue_async_work(env, work->work) != napi_ok) {
      if (work->owner) {
        CancelSetCompilation(work->pending);
      }
      (void)napi_delete_async_work(env, work->work);
      work->work = nullptr;
      RejectDeferred(env, deferred, "Failed to queue RE2Set async work");
      return promise;
    }

    const PendingSetCompilation pending = work->pending;
    const bool owner = work->owner;
    work.release();
    if (owner) {
      try {
        MarkSetCompilationQueued(pending);
      } catch (...) {
        // The queued owner still publishes completion and wakes waiters.
      }
    }
    return promise;
  } catch (const std::exception& error) {
    if (work != nullptr && work->work != nullptr) {
      (void)napi_delete_async_work(env, work->work);
    }
    RejectDeferred(env, deferred, error.what());
    return promise;
  } catch (...) {
    if (work != nullptr && work->work != nullptr) {
      (void)napi_delete_async_work(env, work->work);
    }
    RejectDeferred(env, deferred, "Failed to prepare RE2Set compilation");
    return promise;
  }
}

const char* SetMatchErrorMessage(re2::RE2::Set::ErrorKind error) {
  if (error == re2::RE2::Set::kOutOfMemory) {
    return "RE2Set matching failed: DFA out of memory";
  }
  if (error == re2::RE2::Set::kNotCompiled) {
    return "RE2Set matching failed: set is not compiled";
  }
  if (error == re2::RE2::Set::kInconsistent) {
    return "RE2Set matching failed: inconsistent result";
  }
  return "RE2Set matching failed";
}

bool CreateSetMatchResult(napi_env env, const std::vector<int>& indices, napi_value* result) {
  if (!Check(env, napi_create_array_with_length(env, indices.size(), result), "Failed to create set result")) {
    return false;
  }
  for (size_t index = 0; index < indices.size(); ++index) {
    napi_value element;
    if (!Check(env, napi_create_int32(env, indices[index], &element), "Failed to create pattern index") ||
        !Check(env, napi_set_element(env, *result, index, element), "Failed to write pattern index")) {
      return false;
    }
  }
  return true;
}

template <typename Texts>
void MatchSetBatch(const re2::RE2::Set& set,
                   const Texts& texts,
                   size_t total_bytes,
                   size_t batch_size,
                   std::vector<std::vector<int>>* matches,
                   std::vector<re2::RE2::Set::ErrorKind>* errors) {
  matches->resize(texts.size());
  errors->assign(texts.size(), re2::RE2::Set::kNoError);
  ParallelFor(texts.size(), total_bytes, batch_size, [&](size_t index) {
    re2::RE2::Set::ErrorInfo error_info{re2::RE2::Set::kNoError};
    const bool matched = set.Match(texts[index], &(*matches)[index], &error_info);
    if (!matched) {
      (*errors)[index] = error_info.kind;
    }
  });
}

bool CreateSetBatchResult(napi_env env, const std::vector<std::vector<int>>& matches, napi_value* result) {
  if (!Check(env, napi_create_array_with_length(env, matches.size(), result), "Failed to create set batch result")) {
    return false;
  }
  for (size_t index = 0; index < matches.size(); ++index) {
    napi_value match;
    if (!CreateSetMatchResult(env, matches[index], &match) ||
        !Check(env, napi_set_element(env, *result, index, match), "Failed to write set batch match")) {
      return false;
    }
  }
  return true;
}

napi_value SetTest(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    SetContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kSetContextTypeTag, "Invalid RE2Set context", &context)) {
      return nullptr;
    }
    const re2::RE2::Set& set = *context->set;

    std::string_view text;
    if (!GetText(env, arguments[1], arguments[2], arguments[3], &text)) {
      return nullptr;
    }

    std::vector<int> indices;
    re2::RE2::Set::ErrorInfo error_info{re2::RE2::Set::kNoError};
    const bool matched = set.Match(text, &indices, &error_info);
    if (!matched && error_info.kind != re2::RE2::Set::kNoError) {
      napi_throw_error(env, nullptr, SetMatchErrorMessage(error_info.kind));
      return nullptr;
    }

    napi_value result;
    return CreateSetMatchResult(env, indices, &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

napi_value SetTestMany(napi_env env, napi_callback_info info) {
  std::array<napi_value, 3> arguments;
  if (!GetArgumentsWithOptional<2>(env, info, &arguments)) {
    return nullptr;
  }

  try {
    SetContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kSetContextTypeTag, "Invalid RE2Set context", &context)) {
      return nullptr;
    }
    const re2::RE2::Set& set = *context->set;

    std::vector<std::string_view> texts;
    size_t total_bytes = 0;
    if (!GetTexts(env, arguments[1], &texts, &total_bytes)) {
      return nullptr;
    }
    size_t batch_size = 0;
    if (!GetBatchSize(env, arguments[2], &batch_size)) {
      return nullptr;
    }

    std::vector<std::vector<int>> matches;
    std::vector<re2::RE2::Set::ErrorKind> errors;
    MatchSetBatch(set, texts, total_bytes, batch_size, &matches, &errors);

    for (const re2::RE2::Set::ErrorKind error : errors) {
      if (error != re2::RE2::Set::kNoError) {
        napi_throw_error(env, nullptr, SetMatchErrorMessage(error));
        return nullptr;
      }
    }

    napi_value result;
    return CreateSetBatchResult(env, matches, &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

void SetTestManyExecute(napi_env, void* data) noexcept {
  auto* work = static_cast<SetMatchWork*>(data);
  try {
    MatchSetBatch(*work->set, work->inputs, work->inputs.total_bytes, work->batch_size, &work->matches, &work->errors);
    for (const re2::RE2::Set::ErrorKind error : work->errors) {
      if (error != re2::RE2::Set::kNoError) {
        work->match_error = error;
        break;
      }
    }
    work->errors.clear();
  } catch (...) {
    work->matches.clear();
    work->errors.clear();
    work->failed = true;
  }
}

void SetTestManyComplete(napi_env env, napi_status status, void* data) {
  std::unique_ptr<SetMatchWork> work(static_cast<SetMatchWork*>(data));
  (void)napi_delete_async_work(env, work->work);
  const napi_status release_status = ReleaseAsyncBatch(env, &work->inputs);

  if (status == napi_cancelled) {
    return;
  }
  if (status != napi_ok) {
    RejectDeferred(env, work->deferred, "RE2Set batch matching was cancelled");
    return;
  }
  if (release_status != napi_ok) {
    RejectDeferred(env, work->deferred, "Failed to release unsafe RE2Set batch inputs");
    return;
  }
  if (work->failed) {
    RejectDeferred(env, work->deferred, "RE2Set batch matching failed");
    return;
  }
  if (work->match_error != re2::RE2::Set::kNoError) {
    RejectDeferred(env, work->deferred, SetMatchErrorMessage(work->match_error));
    return;
  }

  napi_value result;
  if (!CreateSetBatchResult(env, work->matches, &result)) {
    RejectDeferred(env, work->deferred, "Failed to create RE2Set batch result");
    return;
  }
  if (napi_resolve_deferred(env, work->deferred, result) != napi_ok) {
    RejectDeferred(env, work->deferred, "Failed to resolve RE2Set batch matching");
  }
}

napi_value SetTestManyAsync(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArgumentsWithOptional<2>(env, info, &arguments)) {
    return nullptr;
  }

  napi_deferred deferred;
  napi_value promise;
  if (!Check(env, napi_create_promise(env, &deferred, &promise), "Failed to create RE2Set batch matching promise")) {
    return nullptr;
  }

  std::unique_ptr<SetMatchWork> work;
  try {
    work = std::make_unique<SetMatchWork>();
    work->deferred = deferred;

    SetContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kSetContextTypeTag, "Invalid RE2Set context", &context)) {
      RejectDeferred(env, deferred, "Failed to read RE2Set context");
      return promise;
    }
    work->set = context->set;

    bool unsafe = false;
    if (!GetBatchSize(env, arguments[2], &work->batch_size) ||
        !GetBoolean(env, arguments[3], "unsafe must be a boolean", &unsafe) ||
        !GetAsyncBatch(env, arguments[1], unsafe, &work->inputs)) {
      RejectDeferred(env, deferred, "Failed to prepare RE2Set batch inputs");
      return promise;
    }

    napi_value resource_name;
    if (napi_create_string_utf8(env, "@nxtedition/re2:test-many-set", NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
        napi_create_async_work(env, nullptr, resource_name, SetTestManyExecute, SetTestManyComplete, work.get(),
                               &work->work) != napi_ok) {
      (void)ReleaseAsyncBatch(env, &work->inputs);
      RejectDeferred(env, deferred, "Failed to create RE2Set batch async work");
      return promise;
    }
    if (napi_queue_async_work(env, work->work) != napi_ok) {
      (void)napi_delete_async_work(env, work->work);
      work->work = nullptr;
      (void)ReleaseAsyncBatch(env, &work->inputs);
      RejectDeferred(env, deferred, "Failed to queue RE2Set batch async work");
      return promise;
    }

    work.release();
    return promise;
  } catch (const std::exception& error) {
    if (work != nullptr) {
      if (work->work != nullptr) {
        (void)napi_delete_async_work(env, work->work);
      }
      (void)ReleaseAsyncBatch(env, &work->inputs);
    }
    RejectDeferred(env, deferred, error.what());
    return promise;
  } catch (...) {
    if (work != nullptr) {
      if (work->work != nullptr) {
        (void)napi_delete_async_work(env, work->work);
      }
      (void)ReleaseAsyncBatch(env, &work->inputs);
    }
    RejectDeferred(env, deferred, "Failed to prepare RE2Set batch matching");
    return promise;
  }
}

bool SetNumberProperty(napi_env env, napi_value object, const char* name, double number) {
  napi_value value;
  return Check(env, napi_create_double(env, number, &value), "Failed to create cache statistic") &&
         Check(env, napi_set_named_property(env, object, name, value), "Failed to write cache statistic");
}

bool SetBigIntProperty(napi_env env, napi_value object, const char* name, uint64_t number) {
  napi_value value;
  return Check(env, napi_create_bigint_uint64(env, number, &value), "Failed to create cache statistic") &&
         Check(env, napi_set_named_property(env, object, name, value), "Failed to write cache statistic");
}

napi_value SetCompileCacheStats(napi_env env, napi_callback_info) {
  try {
    const SetCompileCacheStatsSnapshot stats = GetSetCompileCacheStats();
    napi_value result;
    if (!Check(env, napi_create_object(env, &result), "Failed to create cache statistics") ||
        !SetBigIntProperty(env, result, "compilations", stats.compilations) ||
        !SetBigIntProperty(env, result, "cacheHits", stats.cache_hits) ||
        !SetBigIntProperty(env, result, "deduplications", stats.deduplications) ||
        !SetNumberProperty(env, result, "size", stats.size) ||
        !SetNumberProperty(env, result, "inFlight", stats.in_flight)) {
      return nullptr;
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

}  // namespace

bool RegisterSetBindings(napi_env env, napi_value exports) {
  return ExportFunction(env, exports, "set_init", SetInit) &&
         ExportFunction(env, exports, "set_compile_async", SetCompileAsync) &&
         ExportFunction(env, exports, "set_compile_cache_stats", SetCompileCacheStats) &&
         ExportFunction(env, exports, "set_test", SetTest) &&
         ExportFunction(env, exports, "set_test_many", SetTestMany) &&
         ExportFunction(env, exports, "set_test_many_async", SetTestManyAsync);
}

}  // namespace node_re2
