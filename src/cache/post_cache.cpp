#include "cache/post_cache.h"

#include <stdexcept>
#include <utility>

#include "common/observability.h"

namespace blog::cache {

ThreadSafeLruPostCache::ThreadSafeLruPostCache(std::size_t capacity, std::chrono::milliseconds ttl)
    : capacity_(capacity), ttl_(ttl) {
  if (ttl_.count() <= 0) {
    throw std::invalid_argument("cache TTL must be positive");
  }
}

std::optional<model::Post> ThreadSafeLruPostCache::Get(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = by_id_.find(id);
  if (found == by_id_.end()) {
    observability::Metrics::Instance().RecordCacheMiss();
    return std::nullopt;
  }
  if (found->second->expires_at <= std::chrono::steady_clock::now()) {
    entries_.erase(found->second);
    by_id_.erase(found);
    observability::Metrics::Instance().RecordCacheMiss();
    return std::nullopt;
  }
  entries_.splice(entries_.begin(), entries_, found->second);
  observability::Metrics::Instance().RecordCacheHit();
  return entries_.front().post;
}

void ThreadSafeLruPostCache::Put(const std::string& id, const model::Post& post) {
  if (capacity_ == 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto expiration = std::chrono::steady_clock::now() + ttl_;
  const auto found = by_id_.find(id);
  if (found != by_id_.end()) {
    found->second->post = post;
    found->second->expires_at = expiration;
    entries_.splice(entries_.begin(), entries_, found->second);
    return;
  }
  if (entries_.size() >= capacity_) {
    by_id_.erase(entries_.back().id);
    entries_.pop_back();
    observability::Metrics::Instance().RecordCacheEviction();
  }
  entries_.push_front(Entry{id, post, expiration});
  by_id_[id] = entries_.begin();
}

void ThreadSafeLruPostCache::Invalidate(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = by_id_.find(id);
  if (found == by_id_.end()) return;
  entries_.erase(found->second);
  by_id_.erase(found);
  observability::Metrics::Instance().RecordCacheInvalidation();
}

void ThreadSafeLruPostCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!entries_.empty()) {
    observability::Metrics::Instance().RecordCacheInvalidation(entries_.size());
  }
  entries_.clear();
  by_id_.clear();
}

std::size_t ThreadSafeLruPostCache::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

}  // namespace blog::cache
