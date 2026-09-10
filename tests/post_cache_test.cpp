#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "cache/post_cache.h"

namespace {

blog::model::Post Post(std::string id, std::string title) {
  blog::model::Post post;
  post.id = std::move(id);
  post.title = std::move(title);
  post.author = "tester";
  return post;
}

TEST(PostCacheTest, ReturnsTypedPostsAndEvictsLeastRecentlyUsed) {
  blog::cache::ThreadSafeLruPostCache cache(2, std::chrono::seconds(60));
  cache.Put("one", Post("one", "first"));
  cache.Put("two", Post("two", "second"));
  ASSERT_TRUE(cache.Get("one").has_value());

  cache.Put("three", Post("three", "third"));

  EXPECT_FALSE(cache.Get("two").has_value());
  EXPECT_EQ(cache.Get("one")->title, "first");
  EXPECT_EQ(cache.Get("three")->title, "third");
}

TEST(PostCacheTest, SupportsDisabledCapacity) {
  blog::cache::ThreadSafeLruPostCache cache(0, std::chrono::seconds(60));
  cache.Put("one", Post("one", "first"));
  EXPECT_FALSE(cache.Get("one").has_value());
  EXPECT_EQ(cache.Size(), 0u);
}

TEST(PostCacheTest, ExpiresEntriesAfterTtl) {
  blog::cache::ThreadSafeLruPostCache cache(1,
                                            std::chrono::milliseconds(10));
  cache.Put("one", Post("one", "first"));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(cache.Get("one").has_value());
}

TEST(PostCacheTest, InvalidatesAndUpdatesEntries) {
  blog::cache::ThreadSafeLruPostCache cache(1, std::chrono::seconds(60));
  cache.Put("one", Post("one", "before"));
  cache.Put("one", Post("one", "after"));
  ASSERT_TRUE(cache.Get("one").has_value());
  EXPECT_EQ(cache.Get("one")->title, "after");
  cache.Invalidate("one");
  EXPECT_FALSE(cache.Get("one").has_value());
}

TEST(PostCacheTest, IsSafeUnderConcurrentAccess) {
  blog::cache::ThreadSafeLruPostCache cache(32, std::chrono::seconds(60));
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 8; ++worker) {
    workers.emplace_back([worker, &cache] {
      for (int index = 0; index < 500; ++index) {
        const auto id = std::to_string((worker + index) % 64);
        cache.Put(id, Post(id, "value"));
        cache.Get(id);
        if (index % 11 == 0) cache.Invalidate(id);
      }
    });
  }
  for (auto& worker : workers) worker.join();
  EXPECT_LE(cache.Size(), 32u);
}

TEST(PostCacheTest, RejectsNonPositiveTtl) {
  EXPECT_THROW(blog::cache::ThreadSafeLruPostCache(
                   1, std::chrono::milliseconds(0)),
               std::invalid_argument);
}

}  // namespace

