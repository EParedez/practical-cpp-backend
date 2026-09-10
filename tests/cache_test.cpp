#include <gtest/gtest.h>

#include "cache/lfu_cache.h"
#include "cache/lru_cache.h"
#include "cache/rr_cache.h"

using blog::cache::LFUCache;
using blog::cache::LRUCache;
using blog::cache::RRCache;

// ---------------------------------------------------------------------------
// LRU
// ---------------------------------------------------------------------------
TEST(LRUCacheTest, GetMissingKeyReturnsMinusOne) {
  LRUCache cache(2);
  EXPECT_EQ(cache.get(1), -1);
}

TEST(LRUCacheTest, PutAndGet) {
  LRUCache cache(2);
  cache.put(1, 10);
  cache.put(2, 20);
  EXPECT_EQ(cache.get(1), 10);
  EXPECT_EQ(cache.get(2), 20);
}

TEST(LRUCacheTest, EvictsLeastRecentlyUsed) {
  LRUCache cache(2);
  cache.put(1, 10);
  cache.put(2, 20);
  cache.get(1);          // 1 becomes most recent
  cache.put(3, 30);      // evicts 2
  EXPECT_EQ(cache.get(2), -1);
  EXPECT_EQ(cache.get(1), 10);
  EXPECT_EQ(cache.get(3), 30);
}

TEST(LRUCacheTest, UpdateExistingKey) {
  LRUCache cache(2);
  cache.put(1, 10);
  cache.put(1, 99);
  EXPECT_EQ(cache.get(1), 99);
}

// ---------------------------------------------------------------------------
// LFU
// ---------------------------------------------------------------------------
TEST(LFUCacheTest, GetMissingKeyReturnsMinusOne) {
  LFUCache cache(2);
  EXPECT_EQ(cache.get(1), -1);
}

TEST(LFUCacheTest, PutAndGet) {
  LFUCache cache(3);
  cache.put(1, 10);
  cache.put(2, 20);
  cache.put(3, 30);
  EXPECT_EQ(cache.get(1), 10);
  EXPECT_EQ(cache.get(2), 20);
}

TEST(LFUCacheTest, EvictsLeastFrequentlyUsed) {
  LFUCache cache(2);
  cache.put(1, 10);
  cache.put(2, 20);
  cache.get(1);          // freq(1)=2
  cache.put(3, 30);      // evicts 2 (freq 1)
  EXPECT_EQ(cache.get(2), -1);
  EXPECT_EQ(cache.get(1), 10);
  EXPECT_EQ(cache.get(3), 30);
}

TEST(LFUCacheTest, ZeroCapacityDoesNotStore) {
  LFUCache cache(0);
  cache.put(1, 10);
  EXPECT_EQ(cache.get(1), -1);
}

// ---------------------------------------------------------------------------
// RR
// ---------------------------------------------------------------------------
TEST(RRCacheTest, GetMissingKeyReturnsMinusOne) {
  RRCache cache(2);
  EXPECT_EQ(cache.get(1), -1);
}

TEST(RRCacheTest, PutAndGet) {
  RRCache cache(5);
  cache.put(1, 10);
  cache.put(2, 20);
  EXPECT_EQ(cache.get(1), 10);
  EXPECT_EQ(cache.get(2), 20);
}

TEST(RRCacheTest, UpdateExistingKey) {
  RRCache cache(2);
  cache.put(1, 10);
  cache.put(1, 55);
  EXPECT_EQ(cache.get(1), 55);
}

TEST(RRCacheTest, CapacityIsRespected) {
  RRCache cache(2);
  for (int i = 0; i < 100; ++i) {
    cache.put(i, i * 10);
  }
  int hits = 0;
  for (int i = 0; i < 100; ++i) {
    if (cache.get(i) != -1) ++hits;
  }
  EXPECT_EQ(hits, 2);  // only 2 entries survive
}
