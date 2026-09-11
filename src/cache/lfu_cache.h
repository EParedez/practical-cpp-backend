#pragma once

#include <list>
#include <unordered_map>

namespace blog::cache {

class LFUCache {
 public:
  explicit LFUCache(int capacity) : cap_(capacity), size_(0), min_freq_(0) {}

  int get(int key);
  void put(int key, int value);

 private:
  int cap_;
  int size_;
  int min_freq_;
  std::unordered_map<int, std::pair<int, int>> m_;          // key -> {value, freq}
  std::unordered_map<int, std::list<int>> freq_;            // freq -> keys
  std::unordered_map<int, std::list<int>::iterator> iter_;  // key -> iterator
};

}  // namespace blog::cache
