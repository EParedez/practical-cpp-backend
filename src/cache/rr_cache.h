#pragma once

#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace blog::cache {

class RRCache {
 public:
  explicit RRCache(int capacity) : capacity_(capacity) {}

  int get(int key);
  void put(int key, int value);

 private:
  int capacity_;
  std::vector<int> keys_;
  std::unordered_map<int, int> cache_;
  std::unordered_map<int, std::size_t> key_indexes_;
};

}  // namespace blog::cache
