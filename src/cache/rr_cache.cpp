#include "cache/rr_cache.h"

namespace blog::cache {

int RRCache::get(int key) {
  if (cache_.find(key) == cache_.end()) return -1;
  return cache_[key];
}

void RRCache::put(int key, int value) {
  if (cache_.find(key) != cache_.end()) {
    cache_[key] = value;
    return;
  }
  if (static_cast<int>(keys_.size()) >= capacity_) {
    int victim = keys_[std::rand() % keys_.size()];
    keys_.erase(keys_.begin() + index_of(victim));
    cache_.erase(victim);
  }
  keys_.push_back(key);
  cache_[key] = value;
}

int RRCache::index_of(int key) {
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace blog::cache
