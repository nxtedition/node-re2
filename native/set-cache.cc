#include "set-cache.h"

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <list>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace node_re2 {

struct SetCompilation {
  explicit SetCompilation(std::string encoded_patterns)
      : encoded_patterns(std::move(encoded_patterns)) {}

  std::condition_variable ready;
  std::string encoded_patterns;
  SharedSet set;
  std::string error;
  bool owner_queued = false;
  bool done = false;
  bool unexpected_failure = false;
};

namespace {

constexpr size_t kSetCompileCacheMaxSize = 16;
constexpr size_t kSetCompileCacheMaxKeyBytes = 32 << 20;

struct CachedSet {
  std::string encoded_patterns;
  SharedSet set;
};

struct SetCompileCache {
  std::mutex mutex;
  std::list<CachedSet> entries;
  std::list<PendingSetCompilation> in_flight;
  size_t key_bytes = 0;
  uint64_t compilations = 0;
  uint64_t cache_hits = 0;
  uint64_t deduplications = 0;
};

SetCompileCache& GetSetCompileCache() {
  static SetCompileCache cache;
  return cache;
}

SharedSet FindCachedSetLocked(SetCompileCache& cache, const std::string& key) {
  const auto reverse_entry = std::find_if(
      cache.entries.rbegin(), cache.entries.rend(),
      [&key](const CachedSet& entry) { return entry.encoded_patterns == key; });
  if (reverse_entry == cache.entries.rend()) {
    return nullptr;
  }

  const auto entry = std::prev(reverse_entry.base());
  SharedSet set = entry->set;
  cache.entries.splice(cache.entries.end(), cache.entries, entry);
  ++cache.cache_hits;
  return set;
}

auto FindPendingLocked(SetCompileCache& cache, const std::string& key) {
  return std::find_if(cache.in_flight.begin(), cache.in_flight.end(),
                      [&key](const PendingSetCompilation& pending) {
                        return pending->encoded_patterns == key;
                      });
}

void ErasePendingLocked(SetCompileCache& cache, const PendingSetCompilation& pending) {
  const auto entry = std::find(cache.in_flight.begin(), cache.in_flight.end(), pending);
  if (entry != cache.in_flight.end()) {
    cache.in_flight.erase(entry);
  }
}

template <typename AddPatterns>
SharedSet CompileSetWith(AddPatterns&& add_patterns, std::string* error) {
  re2::RE2::Options options;
  options.set_log_errors(false);
  auto set = std::make_shared<re2::RE2::Set>(options, re2::RE2::UNANCHORED);

  size_t pattern_count = 0;
  const bool added = add_patterns([&](std::string_view pattern) {
    const int pattern_index = set->Add(pattern, error);
    if (pattern_index < 0) {
      return false;
    }
    if (pattern_index != static_cast<int>(pattern_count)) {
      *error = "Unexpected RE2Set pattern index";
      return false;
    }
    ++pattern_count;
    return true;
  });
  if (!added) {
    return nullptr;
  }

  if (!set->Compile()) {
    *error = "Failed to compile RE2Set";
    return nullptr;
  }
  return set;
}

SharedSet CompileEncodedSet(std::string_view encoded_patterns, std::string* error) {
  return CompileSetWith(
      [encoded_patterns](const auto& add) {
        size_t offset = 0;
        while (offset < encoded_patterns.size()) {
          if (encoded_patterns.size() - offset < sizeof(uint64_t)) {
            throw std::logic_error("Invalid encoded RE2Set patterns");
          }
          uint64_t pattern_size = 0;
          for (size_t byte = 0; byte < sizeof(pattern_size); ++byte) {
            pattern_size |= static_cast<uint64_t>(
                                static_cast<unsigned char>(encoded_patterns[offset + byte]))
                            << (byte * 8);
          }
          offset += sizeof(pattern_size);
          if (pattern_size > encoded_patterns.size() - offset) {
            throw std::logic_error("Invalid encoded RE2Set pattern length");
          }
          if (!add(encoded_patterns.substr(offset, static_cast<size_t>(pattern_size)))) {
            return false;
          }
          offset += static_cast<size_t>(pattern_size);
        }
        return true;
      },
      error);
}

}  // namespace

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

SharedSet FindCachedSet(const std::string& key) {
  SetCompileCache& cache = GetSetCompileCache();
  std::lock_guard lock(cache.mutex);
  return FindCachedSetLocked(cache, key);
}

SetCompilationTicket AcquireSetCompilation(std::string key) {
  SetCompileCache& cache = GetSetCompileCache();
  std::unique_lock lock(cache.mutex);
  if (SharedSet set = FindCachedSetLocked(cache, key)) {
    return {.cached_set = std::move(set)};
  }

  const auto existing = FindPendingLocked(cache, key);
  if (existing != cache.in_flight.end()) {
    PendingSetCompilation pending = *existing;
    ++cache.deduplications;
    pending->ready.wait(lock, [&pending] { return pending->owner_queued || pending->done; });
    return {.pending = std::move(pending)};
  }

  auto pending = std::make_shared<SetCompilation>(std::move(key));
  cache.in_flight.push_back(pending);
  ++cache.compilations;
  return {.pending = std::move(pending), .owner = true};
}

void MarkSetCompilationQueued(const PendingSetCompilation& pending) {
  SetCompileCache& cache = GetSetCompileCache();
  {
    std::lock_guard lock(cache.mutex);
    pending->owner_queued = true;
  }
  pending->ready.notify_all();
}

void CancelSetCompilation(const PendingSetCompilation& pending) {
  SetCompileCache& cache = GetSetCompileCache();
  {
    std::lock_guard lock(cache.mutex);
    if (!pending->done) {
      pending->unexpected_failure = true;
      pending->done = true;
    }
    pending->owner_queued = true;
    ErasePendingLocked(cache, pending);
  }
  pending->ready.notify_all();
}

SharedSet CompileSet(const std::vector<std::string>& patterns, std::string* error) {
  return CompileSetWith(
      [&patterns](const auto& add) {
        for (const std::string& pattern : patterns) {
          if (!add(pattern)) {
            return false;
          }
        }
        return true;
      },
      error);
}

SharedSet RunSetCompilation(const PendingSetCompilation& pending,
                            bool* unexpected_failure) noexcept {
  SharedSet set;
  std::string error;
  std::list<CachedSet> evicted;
  bool unexpected = false;
  try {
    set = CompileEncodedSet(pending->encoded_patterns, &error);
  } catch (...) {
    unexpected = true;
    error.clear();
  }

  try {
    SetCompileCache& cache = GetSetCompileCache();
    {
      std::lock_guard lock(cache.mutex);
      pending->set = set;
      pending->error = std::move(error);
      pending->unexpected_failure = unexpected;
      pending->done = true;

      if (set != nullptr) {
        try {
          const size_t key_bytes = pending->encoded_patterns.size();
          cache.entries.push_back({std::move(pending->encoded_patterns), set});
          cache.key_bytes += key_bytes;
          while (cache.entries.size() > kSetCompileCacheMaxSize ||
                 cache.key_bytes > kSetCompileCacheMaxKeyBytes) {
            cache.key_bytes -= cache.entries.front().encoded_patterns.size();
            evicted.splice(evicted.end(), cache.entries, cache.entries.begin());
          }
          ErasePendingLocked(cache, pending);
        } catch (...) {
          pending->unexpected_failure = false;
        }
      }
    }
    pending->ready.notify_all();
    *unexpected_failure = pending->unexpected_failure;
    return set;
  } catch (...) {
    error.clear();
    try {
      CancelSetCompilation(pending);
    } catch (...) {
    }
    *unexpected_failure = true;
    return nullptr;
  }
}

SharedSet WaitForSetCompilation(const PendingSetCompilation& pending) {
  SetCompileCache& cache = GetSetCompileCache();
  std::unique_lock lock(cache.mutex);
  pending->ready.wait(lock, [&pending] { return pending->done; });
  return pending->set;
}

std::string_view SetCompilationError(const PendingSetCompilation& pending) {
  return pending->error;
}

bool SetCompilationFailedUnexpectedly(const PendingSetCompilation& pending) {
  return pending->unexpected_failure;
}

void ReleaseSetCompilation(const PendingSetCompilation& pending) {
  SetCompileCache& cache = GetSetCompileCache();
  std::lock_guard lock(cache.mutex);
  ErasePendingLocked(cache, pending);
}

SetCompileCacheStatsSnapshot GetSetCompileCacheStats() {
  SetCompileCache& cache = GetSetCompileCache();
  std::lock_guard lock(cache.mutex);
  return {
      .compilations = cache.compilations,
      .cache_hits = cache.cache_hits,
      .deduplications = cache.deduplications,
      .size = cache.entries.size(),
      .in_flight = cache.in_flight.size(),
  };
}

}  // namespace node_re2
