#include "regex-binding.h"

#include <re2/re2.h>

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "async-batch.h"
#include "napi-utils.h"
#include "parallel-for.h"

namespace node_re2 {
namespace {

constexpr napi_type_tag kRegexContextTypeTag{UINT64_C(0x5df26165742c4fb1), UINT64_C(0x986d1676ad8e7540)};

using SharedRegex = std::shared_ptr<const re2::RE2>;

struct RegexContext {
  SharedRegex regex;
};

struct RegexMatchWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  SharedRegex regex;
  AsyncBatch inputs;
  size_t batch_size = 0;
  std::vector<uint8_t> matches;
  bool failed = false;
};

template <typename Texts>
void MatchRegexBatch(const re2::RE2& regex,
                     const Texts& texts,
                     size_t total_bytes,
                     size_t batch_size,
                     std::vector<uint8_t>* matches) {
  matches->resize(texts.size());
  ParallelFor(texts.size(), total_bytes, batch_size,
              [&](size_t index) { (*matches)[index] = re2::RE2::PartialMatch(texts[index], regex); });
}

bool CreateRegexBatchResult(napi_env env, const std::vector<uint8_t>& matches, napi_value* result) {
  if (!Check(env, napi_create_array_with_length(env, matches.size(), result), "Failed to create regex batch result")) {
    return false;
  }
  for (size_t index = 0; index < matches.size(); ++index) {
    napi_value match;
    if (!Check(env, napi_get_boolean(env, matches[index] != 0, &match), "Failed to create regex batch match") ||
        !Check(env, napi_set_element(env, *result, index, match), "Failed to write regex batch match")) {
      return false;
    }
  }
  return true;
}

napi_value RegexInit(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    std::string pattern;
    if (!GetPattern(env, arguments[0], &pattern)) {
      return nullptr;
    }

    re2::RE2::Options options;
    options.set_log_errors(false);
    auto regex = std::make_shared<re2::RE2>(pattern, options);
    if (!regex->ok()) {
      napi_throw_error(env, nullptr, regex->error().c_str());
      return nullptr;
    }

    napi_value result;
    auto context = std::make_unique<RegexContext>(RegexContext{std::move(regex)});
    return CreateTaggedExternal(env, std::move(context), &kRegexContextTypeTag, &result) ? result : nullptr;
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
    RegexContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kRegexContextTypeTag, "Invalid RE2 context", &context)) {
      return nullptr;
    }

    std::string_view text;
    if (!GetText(env, arguments[1], arguments[2], arguments[3], &text)) {
      return nullptr;
    }

    napi_value result;
    if (!Check(env, napi_get_boolean(env, re2::RE2::PartialMatch(text, *context->regex), &result),
               "Failed to create match result")) {
      return nullptr;
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

napi_value RegexTestMany(napi_env env, napi_callback_info info) {
  std::array<napi_value, 3> arguments;
  if (!GetArgumentsWithOptional<2>(env, info, &arguments)) {
    return nullptr;
  }

  try {
    RegexContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kRegexContextTypeTag, "Invalid RE2 context", &context)) {
      return nullptr;
    }

    std::vector<std::string_view> texts;
    size_t total_bytes = 0;
    if (!GetTexts(env, arguments[1], &texts, &total_bytes)) {
      return nullptr;
    }
    size_t batch_size = 0;
    if (!GetBatchSize(env, arguments[2], &batch_size)) {
      return nullptr;
    }

    std::vector<uint8_t> matches;
    MatchRegexBatch(*context->regex, texts, total_bytes, batch_size, &matches);

    napi_value result;
    return CreateRegexBatchResult(env, matches, &result) ? result : nullptr;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

void RegexTestManyExecute(napi_env, void* data) noexcept {
  auto* work = static_cast<RegexMatchWork*>(data);
  try {
    MatchRegexBatch(*work->regex, work->inputs, work->inputs.total_bytes, work->batch_size, &work->matches);
  } catch (...) {
    work->matches.clear();
    work->failed = true;
  }
}

void RegexTestManyComplete(napi_env env, napi_status status, void* data) {
  std::unique_ptr<RegexMatchWork> work(static_cast<RegexMatchWork*>(data));
  (void)napi_delete_async_work(env, work->work);
  const napi_status release_status = ReleaseAsyncBatch(env, &work->inputs);

  if (status == napi_cancelled) {
    return;
  }
  if (status != napi_ok) {
    RejectDeferred(env, work->deferred, "RE2 batch matching did not complete");
    return;
  }
  if (release_status != napi_ok) {
    RejectDeferred(env, work->deferred, "Failed to release unsafe RE2 batch inputs");
    return;
  }
  if (work->failed) {
    RejectDeferred(env, work->deferred, "RE2 batch matching failed");
    return;
  }

  napi_value result;
  if (!CreateRegexBatchResult(env, work->matches, &result)) {
    RejectDeferred(env, work->deferred, "Failed to create RE2 batch result");
    return;
  }
  if (napi_resolve_deferred(env, work->deferred, result) != napi_ok) {
    RejectDeferred(env, work->deferred, "Failed to resolve RE2 batch matching");
  }
}

napi_value RegexTestManyAsync(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArgumentsWithOptional<2>(env, info, &arguments)) {
    return nullptr;
  }

  napi_deferred deferred;
  napi_value promise;
  if (!Check(env, napi_create_promise(env, &deferred, &promise), "Failed to create RE2 batch matching promise")) {
    return nullptr;
  }

  std::unique_ptr<RegexMatchWork> work;
  try {
    work = std::make_unique<RegexMatchWork>();
    work->deferred = deferred;

    RegexContext* context = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kRegexContextTypeTag, "Invalid RE2 context", &context)) {
      RejectDeferred(env, deferred, "Failed to read RE2 context");
      return promise;
    }
    work->regex = context->regex;

    bool unsafe = false;
    if (!GetBatchSize(env, arguments[2], &work->batch_size) ||
        !GetBoolean(env, arguments[3], "unsafe must be a boolean", &unsafe) ||
        !GetAsyncBatch(env, arguments[1], unsafe, &work->inputs)) {
      RejectDeferred(env, deferred, "Failed to prepare RE2 batch inputs");
      return promise;
    }

    napi_value resource_name;
    if (napi_create_string_utf8(env, "@nxtedition/re2:test-many", NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
        napi_create_async_work(env, nullptr, resource_name, RegexTestManyExecute, RegexTestManyComplete, work.get(),
                               &work->work) != napi_ok) {
      (void)ReleaseAsyncBatch(env, &work->inputs);
      RejectDeferred(env, deferred, "Failed to create RE2 batch async work");
      return promise;
    }
    if (napi_queue_async_work(env, work->work) != napi_ok) {
      (void)napi_delete_async_work(env, work->work);
      work->work = nullptr;
      (void)ReleaseAsyncBatch(env, &work->inputs);
      RejectDeferred(env, deferred, "Failed to queue RE2 batch async work");
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
    RejectDeferred(env, deferred, "Failed to prepare RE2 batch matching");
    return promise;
  }
}

}  // namespace

bool RegisterRegexBindings(napi_env env, napi_value exports) {
  return ExportFunction(env, exports, "regex_init", RegexInit) &&
         ExportFunction(env, exports, "regex_test", RegexTest) &&
         ExportFunction(env, exports, "regex_test_many", RegexTestMany) &&
         ExportFunction(env, exports, "regex_test_many_async", RegexTestManyAsync);
}

}  // namespace node_re2
