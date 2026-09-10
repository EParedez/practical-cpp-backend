#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace blog::observability {

using LogFields = std::vector<std::pair<std::string, std::string>>;

void Log(const std::string& level, const std::string& event,
         const LogFields& fields = {});

class Metrics {
 public:
  static Metrics& Instance();

  void BeginHttpRequest(const std::string& request_id);
  std::int64_t EndHttpRequest(const std::string& request_id, int status);
  void RecordGrpcRequest(bool failed);
  void RecordMongoOperation(bool failed);
  void RecordCacheHit();
  void RecordCacheMiss();
  void RecordCacheEviction();
  void RecordCacheInvalidation(std::uint64_t count = 1);
  [[nodiscard]] std::string ToPrometheus() const;

 private:
  mutable std::mutex request_mutex_;
  std::unordered_map<
      std::string, std::vector<std::chrono::steady_clock::time_point>>
      request_starts_;
  std::atomic<std::uint64_t> http_requests_{0};
  std::atomic<std::uint64_t> http_errors_{0};
  std::atomic<std::uint64_t> grpc_requests_{0};
  std::atomic<std::uint64_t> grpc_errors_{0};
  std::atomic<std::uint64_t> mongo_operations_{0};
  std::atomic<std::uint64_t> mongo_errors_{0};
  std::atomic<std::uint64_t> cache_hits_{0};
  std::atomic<std::uint64_t> cache_misses_{0};
  std::atomic<std::uint64_t> cache_evictions_{0};
  std::atomic<std::uint64_t> cache_invalidations_{0};
  std::atomic<std::uint64_t> http_duration_ms_total_{0};
};

}  // namespace blog::observability
