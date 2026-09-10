#include "cache/lfu_cache.h"

namespace blog::cache {

int LFUCache::get(int key) {
  if (m_.find(key) == m_.end()) return -1;

  int freq = m_[key].second;
  freq_[freq].erase(iter_[key]);
  if (freq_[freq].empty()) freq_.erase(freq);

  ++m_[key].second;
  int new_freq = m_[key].second;
  freq_[new_freq].push_back(key);
  iter_[key] = --freq_[new_freq].end();

  if (min_freq_ == freq && freq_.find(min_freq_) == freq_.end()) {
    ++min_freq_;
  }
  return m_[key].first;
}

void LFUCache::put(int key, int value) {
  if (cap_ <= 0) return;

  int stored = get(key);
  if (stored != -1) {
    m_[key].first = value;
    return;
  }

  if (size_ >= cap_) {
    int victim = freq_[min_freq_].front();
    freq_[min_freq_].pop_front();
    iter_.erase(victim);
    m_.erase(victim);
    if (freq_[min_freq_].empty()) freq_.erase(min_freq_);
    --size_;
  }

  m_[key] = {value, 1};
  freq_[1].push_back(key);
  iter_[key] = --freq_[1].end();
  min_freq_ = 1;
  ++size_;
}

}  // namespace blog::cache
