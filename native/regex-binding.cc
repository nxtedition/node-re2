#include "regex-binding.h"

#include <re2/re2.h>

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "napi-utils.h"
#include "parallel-for.h"

namespace node_re2 {
namespace {

constexpr napi_type_tag kRegexContextTypeTag{UINT64_C(0x5df26165742c4fb1), UINT64_C(0x986d1676ad8e7540)};

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
    auto regex = std::make_unique<re2::RE2>(pattern, options);
    if (!regex->ok()) {
      napi_throw_error(env, nullptr, regex->error().c_str());
      return nullptr;
    }

    napi_value result;
    return CreateTaggedExternal(env, std::move(regex), &kRegexContextTypeTag, &result) ? result : nullptr;
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
    re2::RE2* regex = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kRegexContextTypeTag, "Invalid RE2 context", &regex)) {
      return nullptr;
    }

    std::string_view text;
    if (!GetText(env, arguments[1], arguments[2], arguments[3], &text)) {
      return nullptr;
    }

    napi_value result;
    if (!Check(env, napi_get_boolean(env, re2::RE2::PartialMatch(text, *regex), &result),
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
    re2::RE2* regex = nullptr;
    if (!GetTaggedExternal(env, arguments[0], &kRegexContextTypeTag, "Invalid RE2 context", &regex)) {
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

    std::vector<uint8_t> matches(texts.size());
    ParallelFor(texts.size(), total_bytes, batch_size,
                [&](size_t index) { matches[index] = re2::RE2::PartialMatch(texts[index], *regex); });

    napi_value result;
    if (!Check(env, napi_create_array_with_length(env, matches.size(), &result),
               "Failed to create regex batch result")) {
      return nullptr;
    }
    for (size_t index = 0; index < matches.size(); ++index) {
      napi_value match;
      if (!Check(env, napi_get_boolean(env, matches[index] != 0, &match), "Failed to create regex batch match") ||
          !Check(env, napi_set_element(env, result, index, match), "Failed to write regex batch match")) {
        return nullptr;
      }
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

}  // namespace

bool RegisterRegexBindings(napi_env env, napi_value exports) {
  return ExportFunction(env, exports, "regex_init", RegexInit) &&
         ExportFunction(env, exports, "regex_test", RegexTest) &&
         ExportFunction(env, exports, "regex_test_many", RegexTestMany);
}

}  // namespace node_re2
