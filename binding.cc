#define NAPI_VERSION 8

#include <assert.h>
#include <napi-macros.h>
#include <node_api.h>

#include <re2/re2.h>
#include <re2/set.h>

#include <memory>

template <typename T>
static void Finalize(napi_env env, void* data, void* hint) {
  if (hint) {
    delete reinterpret_cast<T*>(hint);
  }
}

NAPI_METHOD(regex_init) {
  NAPI_ARGV(1);

  try {
    std::string_view pattern;
    {
      char* buf = nullptr;
      size_t size = 0;
      NAPI_STATUS_THROWS(napi_get_buffer_info(env, argv[0], reinterpret_cast<void**>(&buf), &size));
      pattern = std::string_view(buf, size);
    }

    auto regex = std::make_unique<re2::RE2>(pattern);

    napi_value result;
    NAPI_STATUS_THROWS(napi_create_external(env, regex.get(), Finalize<re2::RE2>, regex.get(), &result));
    regex.release();

    return result;
  } catch (const std::exception& e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}

NAPI_METHOD(regex_test) {
  NAPI_ARGV(4);

  try {
    re2::RE2* regex;
    NAPI_STATUS_THROWS(napi_get_value_external(env, argv[0], reinterpret_cast<void**>(&regex)));

    std::string_view text;
    {
      char* buf = nullptr;
      size_t size = 0;
      NAPI_STATUS_THROWS(napi_get_buffer_info(env, argv[1], reinterpret_cast<void**>(&buf), &size));

      NAPI_INT32(offset, argv[2]);
      offset = std::max(0, std::min<int>(offset, size));

      NAPI_INT32(length, argv[3]);
      length = std::max(0, std::min<int>(length, size - offset));

      text = std::string_view(buf + offset, length);
    }

    napi_value result;
    NAPI_STATUS_THROWS(napi_get_boolean(env, re2::RE2::PartialMatch(text, *regex), &result));
    return result;
  } catch (const std::exception& e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}

NAPI_METHOD(set_init) {
  NAPI_ARGV(1);

  try {
    // TODO (fix): allow options and anchor to be passed in.
    RE2::Options options;
    RE2::Anchor anchor = RE2::Anchor::UNANCHORED;

    auto set = std::make_unique<re2::RE2::Set>(options, anchor);

    auto patterns = argv[0];
    NAPI_FOR_EACH(patterns, pattern) {
      NAPI_BUFFER(buf, pattern);

      std::string error;
      auto idx = set->Add(std::string_view(buf, buf_len), &error) ;
      if (idx < 0) {
        napi_throw_error(env, nullptr, error.c_str());
        return nullptr;
      }

      // TODO (fix): identify pattern with idx and don't
      // assume it's the same as the index in the array.
      assert(idx == i);
    }

    if (!set->Compile()) {
      napi_throw_error(env, nullptr, "Failed to compile set");
      return nullptr;
    }

    napi_value result;
    NAPI_STATUS_THROWS(napi_create_external(env, set.get(), Finalize<re2::RE2::Set>, set.get(), &result));
    set.release();

    return result;
  } catch(const std::exception& e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}

NAPI_METHOD(set_test) {
  NAPI_ARGV(4);

  try {
    re2::RE2::Set* set;
    NAPI_STATUS_THROWS(napi_get_value_external(env, argv[0], reinterpret_cast<void**>(&set)));

    std::string_view text;
    {
      NAPI_BUFFER(buf, argv[1]);

      NAPI_INT32(offset, argv[2]);
      offset = std::max(0, std::min<int>(offset, buf_len));

      NAPI_INT32(length, argv[3]);
      length = std::max(0, std::min<int>(length, buf_len - offset));

      text = std::string_view(buf + offset, length);
    }

    napi_value result;

    std::vector<int> indices;
    if (!set->Match(text, &indices)) {
      NAPI_STATUS_THROWS(napi_get_null(env, &result));
    } else {
      NAPI_STATUS_THROWS(napi_create_array_with_length(env, indices.size(), &result));
      for (size_t n = 0; n < indices.size(); n++) {
        napi_value element;
        NAPI_STATUS_THROWS(napi_create_int32(env, indices[n], &element));
        NAPI_STATUS_THROWS(napi_set_element(env, result, n, element));
      }
    }

    return result;
  } catch (const std::exception& e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}

NAPI_INIT() {
  NAPI_EXPORT_FUNCTION(regex_init);
  NAPI_EXPORT_FUNCTION(regex_test);
  NAPI_EXPORT_FUNCTION(set_init);
  NAPI_EXPORT_FUNCTION(set_test);
}
