#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace node_re2 {

struct TextBatch {
  std::string bytes;
  std::vector<size_t> offsets;

  [[nodiscard]] size_t size() const { return offsets.empty() ? 0 : offsets.size() - 1; }

  [[nodiscard]] std::string_view operator[](size_t index) const {
    const size_t begin = offsets[index];
    return std::string_view(bytes.data() + begin, offsets[index + 1] - begin);
  }
};

}  // namespace node_re2
