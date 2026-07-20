#define NAPI_VERSION 10

#include <node_api.h>

#include <re2/re2.h>
#include <re2/set.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr size_t kSetCompileCacheMaxSize = 16;

struct ByteView {
  const char* data;
  size_t size;
};

using SharedSet = std::shared_ptr<const re2::RE2::Set>;

struct SetContext {
  SharedSet set;
};

struct SetCompileWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  std::vector<std::string> patterns;
  std::string cache_key;
  SharedSet set;
  std::string error;
};

struct InFlightSetCompilation {
  std::condition_variable ready;
  bool done = false;
  SharedSet set;
  std::string error;
};

struct CachedSet {
  SharedSet set;
  std::list<std::string>::iterator position;
};

struct SetCompileCache {
  std::mutex mutex;
  std::list<std::string> lru;
  std::unordered_map<std::string, CachedSet> entries;
  std::unordered_map<std::string, std::shared_ptr<InFlightSetCompilation>> in_flight;
  uint64_t compilations = 0;
  uint64_t cache_hits = 0;
  uint64_t deduplications = 0;
};

SetCompileCache& GetSetCompileCache() {
  static SetCompileCache cache;
  return cache;
}

bool Check(napi_env env, napi_status status, const char* operation) {
  if (status == napi_ok) {
    return true;
  }
  if (status == napi_pending_exception) {
    return false;
  }

  const napi_extended_error_info* info = nullptr;
  const char* message = operation;
  if (napi_get_last_error_info(env, &info) == napi_ok && info != nullptr && info->error_message != nullptr) {
    message = info->error_message;
  }
  napi_throw_error(env, nullptr, message);
  return false;
}

template <size_t N>
bool GetArguments(napi_env env, napi_callback_info info, std::array<napi_value, N>* arguments) {
  size_t count = N;
  if (!Check(env, napi_get_cb_info(env, info, &count, arguments->data(), nullptr, nullptr),
             "Failed to read arguments")) {
    return false;
  }
  if (count < N) {
    napi_throw_type_error(env, nullptr, "Not enough arguments");
    return false;
  }
  return true;
}

bool AssignByteView(napi_env env, void* data, size_t size, ByteView* view) {
  if (size != 0 && data == nullptr) {
    napi_throw_error(env, nullptr, "Binary input has no data");
    return false;
  }
  *view = {size == 0 ? "" : static_cast<const char*>(data), size};
  return true;
}

size_t TypedArrayElementSize(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array:
    case napi_uint8_array:
    case napi_uint8_clamped_array:
      return 1;
    case napi_int16_array:
    case napi_uint16_array:
#ifdef NODE_API_HAS_FLOAT16_ARRAY
    case napi_float16_array:
#endif
      return 2;
    case napi_int32_array:
    case napi_uint32_array:
    case napi_float32_array:
      return 4;
    case napi_float64_array:
    case napi_bigint64_array:
    case napi_biguint64_array:
      return 8;
  }
  return 0;
}

bool GetByteView(napi_env env, napi_value value, ByteView* view) {
  bool is_buffer = false;
  if (!Check(env, napi_is_buffer(env, value, &is_buffer), "Failed to inspect binary input")) {
    return false;
  }
  if (is_buffer) {
    void* data = nullptr;
    size_t size = 0;
    if (!Check(env, napi_get_buffer_info(env, value, &data, &size), "Failed to read Buffer")) {
      return false;
    }
    return AssignByteView(env, data, size, view);
  }

  bool is_typed_array = false;
  if (!Check(env, napi_is_typedarray(env, value, &is_typed_array), "Failed to inspect binary input")) {
    return false;
  }
  if (is_typed_array) {
    napi_typedarray_type type;
    size_t length = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env, napi_get_typedarray_info(env, value, &type, &length, &data, &array_buffer, &byte_offset),
               "Failed to read TypedArray")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    const size_t element_size = TypedArrayElementSize(type);
    if (element_size == 0 || length > std::numeric_limits<size_t>::max() / element_size) {
      napi_throw_range_error(env, nullptr, "TypedArray is too large");
      return false;
    }
    const size_t size = length * element_size;
    return AssignByteView(env, data, size, view);
  }

  bool is_data_view = false;
  if (!Check(env, napi_is_dataview(env, value, &is_data_view), "Failed to inspect binary input")) {
    return false;
  }
  if (is_data_view) {
    size_t size = 0;
    void* data = nullptr;
    napi_value array_buffer;
    size_t byte_offset = 0;
    if (!Check(env, napi_get_dataview_info(env, value, &size, &data, &array_buffer, &byte_offset),
               "Failed to read DataView")) {
      return false;
    }
    (void)array_buffer;
    (void)byte_offset;
    return AssignByteView(env, data, size, view);
  }

  napi_throw_type_error(env, nullptr, "Expected a Buffer, TypedArray, or DataView");
  return false;
}

bool GetClampedIndex(napi_env env, napi_value value, size_t maximum, size_t* result) {
  double number = 0;
  const napi_status status = napi_get_value_double(env, value, &number);
  if (status != napi_ok) {
    if (status == napi_pending_exception) {
      return false;
    }
    napi_throw_type_error(env, nullptr, "Byte ranges must be numbers");
    return false;
  }

  if (std::isnan(number)) {
    *result = 0;
  } else if (number <= 0) {
    *result = 0;
  } else if (number >= static_cast<double>(maximum)) {
    *result = maximum;
  } else {
    *result = static_cast<size_t>(std::trunc(number));
  }
  return true;
}

bool GetText(napi_env env, napi_value input, napi_value offset_value, napi_value length_value, std::string_view* text) {
  ByteView view;
  if (!GetByteView(env, input, &view)) {
    return false;
  }

  size_t offset = 0;
  if (!GetClampedIndex(env, offset_value, view.size, &offset)) {
    return false;
  }
  size_t length = 0;
  if (!GetClampedIndex(env, length_value, view.size - offset, &length)) {
    return false;
  }

  *text = std::string_view(view.data + offset, length);
  return true;
}

bool GetPatterns(napi_env env, napi_value value, std::vector<std::string>* patterns) {
  bool is_array = false;
  if (!Check(env, napi_is_array(env, value, &is_array), "Failed to inspect patterns")) {
    return false;
  }
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "patterns must be an array");
    return false;
  }

  uint32_t pattern_count = 0;
  if (!Check(env, napi_get_array_length(env, value, &pattern_count), "Failed to read patterns")) {
    return false;
  }
  if (pattern_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    napi_throw_range_error(env, nullptr, "Too many patterns");
    return false;
  }

  patterns->reserve(pattern_count);
  for (uint32_t index = 0; index < pattern_count; ++index) {
    napi_value pattern_value;
    if (!Check(env, napi_get_element(env, value, index, &pattern_value), "Failed to read pattern")) {
      return false;
    }
    ByteView pattern;
    if (!GetByteView(env, pattern_value, &pattern)) {
      return false;
    }
    patterns->emplace_back(pattern.data, pattern.size);
  }
  return true;
}

bool GetTexts(napi_env env, napi_value value, std::vector<std::string_view>* texts) {
  bool is_array = false;
  if (!Check(env, napi_is_array(env, value, &is_array), "Failed to inspect inputs")) {
    return false;
  }
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "inputs must be an array");
    return false;
  }

  uint32_t input_count = 0;
  if (!Check(env, napi_get_array_length(env, value, &input_count), "Failed to read inputs")) {
    return false;
  }

  texts->reserve(input_count);
  for (uint32_t index = 0; index < input_count; ++index) {
    napi_value input;
    if (!Check(env, napi_get_element(env, value, index, &input), "Failed to read input")) {
      return false;
    }
    ByteView view;
    if (!GetByteView(env, input, &view)) {
      return false;
    }
    texts->emplace_back(view.data, view.size);
  }
  return true;
}

template <typename Function>
void ParallelFor(size_t size, Function&& function) {
#ifdef _OPENMP
  constexpr size_t kInputsPerThread = 64;
  constexpr int kMaxThreads = 8;
  const int thread_count = std::min(
      {omp_get_max_threads(), kMaxThreads, static_cast<int>((size + kInputsPerThread - 1) / kInputsPerThread)});
  if (thread_count > 1) {
    std::atomic<bool> stopped{false};
    std::mutex error_mutex;
    std::exception_ptr error;
#pragma omp parallel for schedule(dynamic, 8) num_threads(thread_count)
    for (int64_t index = 0; index < static_cast<int64_t>(size); ++index) {
      if (!stopped.load(std::memory_order_relaxed)) {
        try {
          function(static_cast<size_t>(index));
        } catch (...) {
          stopped.store(true, std::memory_order_relaxed);
          std::lock_guard lock(error_mutex);
          if (error == nullptr) {
            error = std::current_exception();
          }
        }
      }
    }
    if (error != nullptr) {
      std::rethrow_exception(error);
    }
    return;
  }
#endif

  for (size_t index = 0; index < size; ++index) {
    function(index);
  }
}

std::string SetCompileCacheKey(const std::vector<std::string>& patterns) {
  size_t key_size = 0;
  for (const std::string& pattern : patterns) {
    if (key_size > std::numeric_limits<size_t>::max() - sizeof(uint64_t) ||
        pattern.size() > std::numeric_limits<size_t>::max() - key_size - sizeof(uint64_t)) {
      throw std::length_error("Pattern set is too large");
    }
    key_size += sizeof(uint64_t) + pattern.size();
  }

  std::string key;
  key.reserve(key_size);
  for (const std::string& pattern : patterns) {
    const uint64_t pattern_size = pattern.size();
    for (size_t byte = 0; byte < sizeof(pattern_size); ++byte) {
      key.push_back(static_cast<char>((pattern_size >> (byte * 8)) & UINT64_C(0xff)));
    }
    key.append(pattern);
  }
  return key;
}

SharedSet FindCachedSetLocked(SetCompileCache& cache, const std::string& key) {
  const auto entry = cache.entries.find(key);
  if (entry == cache.entries.end()) {
    return nullptr;
  }
  cache.lru.splice(cache.lru.end(), cache.lru, entry->second.position);
  ++cache.cache_hits;
  return entry->second.set;
}

SharedSet FindCachedSet(const std::string& key) {
  SetCompileCache& cache = GetSetCompileCache();
  std::lock_guard lock(cache.mutex);
  return FindCachedSetLocked(cache, key);
}

void StoreCachedSetLocked(SetCompileCache& cache, const std::string& key, SharedSet set) {
  const auto existing = cache.entries.find(key);
  if (existing != cache.entries.end()) {
    existing->second.set = std::move(set);
    cache.lru.splice(cache.lru.end(), cache.lru, existing->second.position);
    return;
  }

  cache.lru.push_back(key);
  auto position = cache.lru.end();
  --position;
  try {
    cache.entries.emplace(key, CachedSet{std::move(set), position});
  } catch (...) {
    cache.lru.pop_back();
    throw;
  }

  while (cache.entries.size() > kSetCompileCacheMaxSize) {
    cache.entries.erase(cache.lru.front());
    cache.lru.pop_front();
  }
}

SharedSet CompileSet(const std::vector<std::string>& patterns, std::string* error) {
  re2::RE2::Options options;
  options.set_log_errors(false);
  auto set = std::make_shared<re2::RE2::Set>(options, re2::RE2::UNANCHORED);

  for (size_t index = 0; index < patterns.size(); ++index) {
    const int pattern_index = set->Add(patterns[index], error);
    if (pattern_index < 0) {
      return nullptr;
    }
    if (pattern_index != static_cast<int>(index)) {
      *error = "Unexpected RE2Set pattern index";
      return nullptr;
    }
  }

  if (!set->Compile()) {
    *error = "Failed to compile RE2Set";
    return nullptr;
  }
  return set;
}

SharedSet GetOrCompileSet(const std::vector<std::string>& patterns, const std::string& key, std::string* error) {
  SetCompileCache& cache = GetSetCompileCache();
  std::shared_ptr<InFlightSetCompilation> state;

  {
    std::unique_lock lock(cache.mutex);
    if (SharedSet set = FindCachedSetLocked(cache, key)) {
      return set;
    }

    const auto in_flight = cache.in_flight.find(key);
    if (in_flight != cache.in_flight.end()) {
      state = in_flight->second;
      ++cache.deduplications;
      state->ready.wait(lock, [&state] { return state->done; });
      *error = state->error;
      return state->set;
    }

    state = std::make_shared<InFlightSetCompilation>();
    cache.in_flight.emplace(key, state);
    ++cache.compilations;
  }

  SharedSet set;
  std::string compilation_error;
  try {
    set = CompileSet(patterns, &compilation_error);
  } catch (const std::exception& exception) {
    compilation_error = exception.what();
  } catch (...) {
    compilation_error = "Unknown RE2Set compilation failure";
  }

  {
    std::lock_guard lock(cache.mutex);
    if (set != nullptr) {
      try {
        StoreCachedSetLocked(cache, key, set);
      } catch (...) {
        // A cache allocation failure must not discard a compiled result.
      }
    }
    state->set = set;
    state->error = std::move(compilation_error);
    state->done = true;
    cache.in_flight.erase(key);
  }
  state->ready.notify_all();

  *error = state->error;
  return set;
}

template <typename T>
void Finalize(napi_env, void* data, void*) {
  delete static_cast<T*>(data);
}

template <typename T>
bool CreateExternal(napi_env env, std::unique_ptr<T> value, napi_value* result) {
  if (!Check(env, napi_create_external(env, value.get(), Finalize<T>, nullptr, result),
             "Failed to create native context")) {
    return false;
  }
  value.release();
  return true;
}

napi_status CreateSetExternalRaw(napi_env env, SharedSet set, napi_value* result) {
  auto context = std::make_unique<SetContext>(SetContext{std::move(set)});
  const napi_status status = napi_create_external(env, context.get(), Finalize<SetContext>, nullptr, result);
  if (status == napi_ok) {
    context.release();
  }
  return status;
}

bool CreateSetExternal(napi_env env, SharedSet set, napi_value* result) {
  return Check(env, CreateSetExternalRaw(env, std::move(set), result), "Failed to create native RE2Set context");
}

napi_value RegexInit(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    ByteView pattern;
    if (!GetByteView(env, arguments[0], &pattern)) {
      return nullptr;
    }

    re2::RE2::Options options;
    options.set_log_errors(false);
    auto regex = std::make_unique<re2::RE2>(std::string_view(pattern.data, pattern.size), options);
    if (!regex->ok()) {
      napi_throw_error(env, nullptr, regex->error().c_str());
      return nullptr;
    }

    napi_value result;
    return CreateExternal(env, std::move(regex), &result) ? result : nullptr;
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
    if (!Check(env, napi_get_value_external(env, arguments[0], reinterpret_cast<void**>(&regex)),
               "Failed to read regex context")) {
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
  std::array<napi_value, 2> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    re2::RE2* regex = nullptr;
    if (!Check(env, napi_get_value_external(env, arguments[0], reinterpret_cast<void**>(&regex)),
               "Failed to read regex context")) {
      return nullptr;
    }

    std::vector<std::string_view> texts;
    if (!GetTexts(env, arguments[1], &texts)) {
      return nullptr;
    }

    std::vector<uint8_t> matches(texts.size());
    ParallelFor(texts.size(), [&](size_t index) { matches[index] = re2::RE2::PartialMatch(texts[index], *regex); });

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

void SetCompileExecute(napi_env, void* data) {
  auto* work = static_cast<SetCompileWork*>(data);
  try {
    work->set = GetOrCompileSet(work->patterns, work->cache_key, &work->error);
  } catch (const std::exception& error) {
    work->error = error.what();
  } catch (...) {
    work->error = "Unknown RE2Set compilation failure";
  }
}

bool TakePendingException(napi_env env, napi_value* exception) {
  bool pending = false;
  return napi_is_exception_pending(env, &pending) == napi_ok && pending &&
         napi_get_and_clear_last_exception(env, exception) == napi_ok;
}

void RejectDeferred(napi_env env, napi_deferred deferred, std::string_view message) {
  napi_value reason;
  if (TakePendingException(env, &reason)) {
    (void)napi_reject_deferred(env, deferred, reason);
    return;
  }

  napi_value message_value;
  if (napi_create_string_utf8(env, message.data(), message.size(), &message_value) != napi_ok ||
      napi_create_error(env, nullptr, message_value, &reason) != napi_ok) {
    if (!TakePendingException(env, &reason) && napi_get_undefined(env, &reason) != napi_ok) {
      return;
    }
  }
  (void)napi_reject_deferred(env, deferred, reason);
}

void SetCompileComplete(napi_env env, napi_status status, void* data) {
  std::unique_ptr<SetCompileWork> work(static_cast<SetCompileWork*>(data));
  (void)napi_delete_async_work(env, work->work);

  if (status != napi_ok) {
    RejectDeferred(env, work->deferred, "RE2Set compilation was cancelled");
    return;
  }
  if (work->set == nullptr) {
    RejectDeferred(env, work->deferred, work->error.empty() ? "Failed to compile RE2Set" : work->error);
    return;
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

napi_value SetCompileAsync(napi_env env, napi_callback_info info) {
  std::array<napi_value, 1> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    auto work = std::make_unique<SetCompileWork>();
    if (!GetPatterns(env, arguments[0], &work->patterns)) {
      return nullptr;
    }
    work->cache_key = SetCompileCacheKey(work->patterns);
    SharedSet cached_set = FindCachedSet(work->cache_key);

    napi_value promise;
    if (!Check(env, napi_create_promise(env, &work->deferred, &promise),
               "Failed to create RE2Set compilation promise")) {
      return nullptr;
    }

    try {
      if (cached_set != nullptr) {
        napi_value context;
        if (CreateSetExternalRaw(env, std::move(cached_set), &context) != napi_ok) {
          RejectDeferred(env, work->deferred, "Failed to create native RE2Set context");
          return promise;
        }
        if (napi_resolve_deferred(env, work->deferred, context) != napi_ok) {
          RejectDeferred(env, work->deferred, "Failed to resolve cached RE2Set compilation");
        }
        return promise;
      }

      napi_value resource_name;
      if (napi_create_string_utf8(env, "@nxtedition/re2:compile-set", NAPI_AUTO_LENGTH, &resource_name) != napi_ok) {
        RejectDeferred(env, work->deferred, "Failed to create RE2Set async resource name");
        return promise;
      }
      if (napi_create_async_work(env, nullptr, resource_name, SetCompileExecute, SetCompileComplete, work.get(),
                                 &work->work) != napi_ok) {
        RejectDeferred(env, work->deferred, "Failed to create RE2Set async work");
        return promise;
      }

      if (napi_queue_async_work(env, work->work) != napi_ok) {
        (void)napi_delete_async_work(env, work->work);
        work->work = nullptr;
        RejectDeferred(env, work->deferred, "Failed to queue RE2Set async work");
        return promise;
      }

      work.release();
      return promise;
    } catch (const std::exception& error) {
      if (work->work != nullptr) {
        (void)napi_delete_async_work(env, work->work);
      }
      RejectDeferred(env, work->deferred, error.what());
      return promise;
    } catch (...) {
      if (work->work != nullptr) {
        (void)napi_delete_async_work(env, work->work);
      }
      RejectDeferred(env, work->deferred, "Failed to prepare RE2Set compilation");
      return promise;
    }
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
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

napi_value SetTest(napi_env env, napi_callback_info info) {
  std::array<napi_value, 4> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    SetContext* context = nullptr;
    if (!Check(env, napi_get_value_external(env, arguments[0], reinterpret_cast<void**>(&context)),
               "Failed to read set context")) {
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
  std::array<napi_value, 2> arguments;
  if (!GetArguments(env, info, &arguments)) {
    return nullptr;
  }

  try {
    SetContext* context = nullptr;
    if (!Check(env, napi_get_value_external(env, arguments[0], reinterpret_cast<void**>(&context)),
               "Failed to read set context")) {
      return nullptr;
    }
    const re2::RE2::Set& set = *context->set;

    std::vector<std::string_view> texts;
    if (!GetTexts(env, arguments[1], &texts)) {
      return nullptr;
    }

    std::vector<std::vector<int>> matches(texts.size());
    std::vector<re2::RE2::Set::ErrorKind> errors(texts.size(), re2::RE2::Set::kNoError);
    ParallelFor(texts.size(), [&](size_t index) {
      re2::RE2::Set::ErrorInfo error_info{re2::RE2::Set::kNoError};
      const bool matched = set.Match(texts[index], &matches[index], &error_info);
      if (!matched) {
        errors[index] = error_info.kind;
      }
    });

    for (const re2::RE2::Set::ErrorKind error : errors) {
      if (error != re2::RE2::Set::kNoError) {
        napi_throw_error(env, nullptr, SetMatchErrorMessage(error));
        return nullptr;
      }
    }

    napi_value result;
    if (!Check(env, napi_create_array_with_length(env, matches.size(), &result), "Failed to create set batch result")) {
      return nullptr;
    }
    for (size_t index = 0; index < matches.size(); ++index) {
      napi_value match;
      if (!CreateSetMatchResult(env, matches[index], &match) ||
          !Check(env, napi_set_element(env, result, index, match), "Failed to write set batch match")) {
        return nullptr;
      }
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

bool SetNumberProperty(napi_env env, napi_value object, const char* name, double number) {
  napi_value value;
  return Check(env, napi_create_double(env, number, &value), "Failed to create cache statistic") &&
         Check(env, napi_set_named_property(env, object, name, value), "Failed to write cache statistic");
}

napi_value SetCompileCacheStats(napi_env env, napi_callback_info) {
  try {
    uint64_t compilations = 0;
    uint64_t cache_hits = 0;
    uint64_t deduplications = 0;
    size_t size = 0;
    size_t in_flight = 0;
    {
      SetCompileCache& cache = GetSetCompileCache();
      std::lock_guard lock(cache.mutex);
      compilations = cache.compilations;
      cache_hits = cache.cache_hits;
      deduplications = cache.deduplications;
      size = cache.entries.size();
      in_flight = cache.in_flight.size();
    }

    napi_value result;
    if (!Check(env, napi_create_object(env, &result), "Failed to create cache statistics") ||
        !SetNumberProperty(env, result, "compilations", compilations) ||
        !SetNumberProperty(env, result, "cacheHits", cache_hits) ||
        !SetNumberProperty(env, result, "deduplications", deduplications) ||
        !SetNumberProperty(env, result, "size", size) || !SetNumberProperty(env, result, "inFlight", in_flight)) {
      return nullptr;
    }
    return result;
  } catch (const std::exception& error) {
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
  }
}

bool ExportFunction(napi_env env, napi_value exports, const char* name, napi_callback callback) {
  napi_value function;
  return Check(env, napi_create_function(env, name, NAPI_AUTO_LENGTH, callback, nullptr, &function),
               "Failed to create native function") &&
         Check(env, napi_set_named_property(env, exports, name, function), "Failed to export native function");
}

}  // namespace

NAPI_MODULE_INIT() {
  if (!ExportFunction(env, exports, "regex_init", RegexInit) ||
      !ExportFunction(env, exports, "regex_test", RegexTest) ||
      !ExportFunction(env, exports, "regex_test_many", RegexTestMany) ||
      !ExportFunction(env, exports, "set_init", SetInit) ||
      !ExportFunction(env, exports, "set_compile_async", SetCompileAsync) ||
      !ExportFunction(env, exports, "set_compile_cache_stats", SetCompileCacheStats) ||
      !ExportFunction(env, exports, "set_test", SetTest) ||
      !ExportFunction(env, exports, "set_test_many", SetTestMany)) {
    return nullptr;
  }
  return exports;
}
