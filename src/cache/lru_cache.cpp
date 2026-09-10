#include "cache/lru_cache.h"

namespace blog::cache {

int LRUCache::get(int key) {
  if (cache_.find(key) == cache_.end()) return -1;
  moveToFront(key);
  return cache_[key].first;
}

void LRUCache::put(int key, int value) {
  if (cache_.find(key) != cache_.end()) {
    removeKey(key);
  } else if (static_cast<int>(cache_.size()) == capacity_) {
    removeKey(lru_.back());
  }
  addToFront(key, value);
}

void LRUCache::addToFront(int key, int value) {
  lru_.push_front(key);
  cache_[key] = {value, lru_.begin()};
}

void LRUCache::removeKey(int key) {
  lru_.erase(cache_[key].second);
  cache_.erase(key);
}

void LRUCache::moveToFront(int key) {
  lru_.erase(cache_[key].second);
  lru_.push_front(key);
  cache_[key].second = lru_.begin();
}

}  // namespace blog::cache
