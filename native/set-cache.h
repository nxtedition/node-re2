#pragma once

#include <re2/set.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace node_re2 {

using SharedSet = std::shared_ptr<const re2::RE2::Set>;

struct SetContext {
  SharedSet set;
};

struct SetCompilation;
using PendingSetCompilation = std::shared_ptr<SetCompilation>;

struct SetCompilationTicket {
  SharedSet cached_set;
  PendingSetCompilation pending;
  bool owner = false;
};

struct SetCompileCacheStatsSnapshot {
  uint64_t compilations;
  uint64_t cache_hits;
  uint64_t deduplications;
  size_t size;
  size_t in_flight;
};

std::string SetCompileCacheKey(const std::vector<std::string>& patterns);
SharedSet FindCachedSet(const std::string& key);
SetCompilationTicket AcquireSetCompilation(std::string key);
void MarkSetCompilationQueued(const PendingSetCompilation& pending);
void CancelSetCompilation(const PendingSetCompilation& pending);
SharedSet CompileSet(const std::vector<std::string>& patterns, std::string* error);
SharedSet RunSetCompilation(const PendingSetCompilation& pending, bool* unexpected_failure) noexcept;
SharedSet WaitForSetCompilation(const PendingSetCompilation& pending);
std::string_view SetCompilationError(const PendingSetCompilation& pending);
bool SetCompilationFailedUnexpectedly(const PendingSetCompilation& pending);
void ReleaseSetCompilation(const PendingSetCompilation& pending);
SetCompileCacheStatsSnapshot GetSetCompileCacheStats();

}  // namespace node_re2
