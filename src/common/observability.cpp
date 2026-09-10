#include "common/observability.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace blog::observability {
namespace {

std::string EscapeJson(const std::string& input) {
  std::ostringstream escaped;
  for (const unsigned char character : input) {
    switch (character) {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (character < 0x20) {
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(character) << std::dec;
        } else {
          escaped << character;
        }
    }
  }
  return escaped.str();
}

std::string Timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

}  // namespace

void Log(const std::string& level, const std::string& event,
         const LogFields& fields) {
  static std::mutex log_mutex;
  std::lock_guard<std::mutex> lock(log_mutex);
  std::clog << "{\"timestamp\":\"" << Timestamp() << "\",\"level\":\""
            << EscapeJson(level) << "\",\"event\":\"" << EscapeJson(event)
            << '"';
  for (const auto& [name, value] : fields) {
    std::clog << ",\"" << EscapeJson(name) << "\":\"" << EscapeJson(value)
              << '"';
  }
  std::clog << "}" << std::endl;
}

Metrics& Metrics::Instance() {
  static Metrics metrics;
  return metrics;
}

void Metrics::BeginHttpRequest(const std::string& request_id) {
  std::lock_guard<std::mutex> lock(request_mutex_);
  request_starts_[request_id].push_back(std::chrono::steady_clock::now());
}

std::int64_t Metrics::EndHttpRequest(const std::string& request_id, int status) {
  std::int64_t duration_ms = 0;
  {
    std::lock_guard<std::mutex> lock(request_mutex_);
    const auto found = request_starts_.find(request_id);
    if (found != request_starts_.end() && !found->second.empty()) {
      duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - found->second.back())
                        .count();
      found->second.pop_back();
      if (found->second.empty()) request_starts_.erase(found);
    }
  }
  ++http_requests_;
  if (status >= 400) ++http_errors_;
  if (duration_ms > 0) {
    http_duration_ms_total_.fetch_add(static_cast<std::uint64_t>(duration_ms));
  }
  return duration_ms;
}

void Metrics::RecordGrpcRequest(bool failed) {
  ++grpc_requests_;
  if (failed) ++grpc_errors_;
}

void Metrics::RecordMongoOperation(bool failed) {
  ++mongo_operations_;
  if (failed) ++mongo_errors_;
}

void Metrics::RecordCacheHit() { ++cache_hits_; }

void Metrics::RecordCacheMiss() { ++cache_misses_; }

void Metrics::RecordCacheEviction() { ++cache_evictions_; }

void Metrics::RecordCacheInvalidation(std::uint64_t count) {
  cache_invalidations_.fetch_add(count);
}

std::string Metrics::ToPrometheus() const {
  std::ostringstream output;
  output << "# TYPE blog_http_requests_total counter\n"
         << "blog_http_requests_total " << http_requests_.load() << "\n"
         << "# TYPE blog_http_errors_total counter\n"
         << "blog_http_errors_total " << http_errors_.load() << "\n"
         << "# TYPE blog_http_request_duration_milliseconds_total counter\n"
         << "blog_http_request_duration_milliseconds_total "
         << http_duration_ms_total_.load() << "\n"
         << "# TYPE blog_grpc_requests_total counter\n"
         << "blog_grpc_requests_total " << grpc_requests_.load() << "\n"
         << "# TYPE blog_grpc_errors_total counter\n"
         << "blog_grpc_errors_total " << grpc_errors_.load() << "\n"
         << "# TYPE blog_mongo_operations_total counter\n"
         << "blog_mongo_operations_total " << mongo_operations_.load() << "\n"
         << "# TYPE blog_mongo_errors_total counter\n"
         << "blog_mongo_errors_total " << mongo_errors_.load() << "\n"
         << "# TYPE blog_cache_hits_total counter\n"
         << "blog_cache_hits_total " << cache_hits_.load() << "\n"
         << "# TYPE blog_cache_misses_total counter\n"
         << "blog_cache_misses_total " << cache_misses_.load() << "\n"
         << "# TYPE blog_cache_evictions_total counter\n"
         << "blog_cache_evictions_total " << cache_evictions_.load() << "\n"
         << "# TYPE blog_cache_invalidations_total counter\n"
         << "blog_cache_invalidations_total " << cache_invalidations_.load()
         << "\n";
  return output.str();
}

}  // namespace blog::observability
