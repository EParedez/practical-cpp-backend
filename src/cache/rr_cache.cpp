#include "cache/rr_cache.h"

namespace blog::cache {

int RRCache::get(int key) {
  if (cache_.find(key) == cache_.end()) return -1;
  return cache_[key];
}

void RRCache::put(int key, int value) {
  if (capacity_ <= 0) return;
  if (cache_.find(key) != cache_.end()) {
    cache_[key] = value;
    return;
  }
  if (static_cast<int>(keys_.size()) >= capacity_) {
    const auto victim_index = static_cast<std::size_t>(std::rand()) % keys_.size();
    const int victim = keys_[victim_index];
    const int last_key = keys_.back();
    keys_[victim_index] = last_key;
    key_indexes_[last_key] = victim_index;
    keys_.pop_back();
    key_indexes_.erase(victim);
    cache_.erase(victim);
  }
  keys_.push_back(key);
  key_indexes_[key] = keys_.size() - 1;
  cache_[key] = value;
}

}  // namespace blog::cache
