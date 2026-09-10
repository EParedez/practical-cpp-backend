#pragma once

#include <list>
#include <unordered_map>

namespace blog::cache {

class LRUCache {
 public:
  explicit LRUCache(int capacity) : capacity_(capacity) {}

  int get(int key);
  void put(int key, int value);

 private:
  using Pair = std::pair<int, int>;
  using ListIterator = std::list<int>::iterator;

  std::unordered_map<int, std::pair<int, ListIterator>> cache_;
  std::list<int> lru_;
  int capacity_;

  void addToFront(int key, int value);
  void removeKey(int key);
  void moveToFront(int key);
};

}  // namespace blog::cache
