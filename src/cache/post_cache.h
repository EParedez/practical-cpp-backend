#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "model/blog_models.h"

namespace blog::cache {

class PostCache {
 public:
  virtual ~PostCache() = default;

  virtual std::optional<model::Post> Get(const std::string& id) = 0;
  virtual void Put(const std::string& id, const model::Post& post) = 0;
  virtual void Invalidate(const std::string& id) = 0;
  virtual void Clear() = 0;
  [[nodiscard]] virtual std::size_t Size() const = 0;
};

class ThreadSafeLruPostCache final : public PostCache {
 public:
  ThreadSafeLruPostCache(std::size_t capacity,
                         std::chrono::milliseconds ttl);

  std::optional<model::Post> Get(const std::string& id) override;
  void Put(const std::string& id, const model::Post& post) override;
  void Invalidate(const std::string& id) override;
  void Clear() override;
  [[nodiscard]] std::size_t Size() const override;

 private:
  struct Entry {
    std::string id;
    model::Post post;
    std::chrono::steady_clock::time_point expires_at;
  };

  using EntryIterator = std::list<Entry>::iterator;

  const std::size_t capacity_;
  const std::chrono::milliseconds ttl_;
  mutable std::mutex mutex_;
  std::list<Entry> entries_;
  std::unordered_map<std::string, EntryIterator> by_id_;
};

}  // namespace blog::cache

