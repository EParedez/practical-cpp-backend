#include "config/app_config.h"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace blog::config {
namespace {

int ParseInteger(const std::string& name, const std::string& raw, int minimum,
                 int maximum) {
  try {
    std::size_t parsed = 0;
    const long value = std::stol(raw, &parsed);
    if (parsed != raw.size() || value < minimum || value > maximum) {
      throw std::invalid_argument("out of range");
    }
    return static_cast<int>(value);
  } catch (const std::exception&) {
    throw std::invalid_argument(name + " must be an integer between " +
                                std::to_string(minimum) + " and " +
                                std::to_string(maximum));
  }
}

void AssignString(const EnvironmentReader& reader, const std::string& name,
                  std::string* target) {
  if (const auto value = reader(name)) *target = *value;
}

void AssignInteger(const EnvironmentReader& reader, const std::string& name,
                   int minimum, int maximum, int* target) {
  if (const auto value = reader(name)) {
    *target = ParseInteger(name, *value, minimum, maximum);
  }
}

}  // namespace

AppConfig LoadFromEnvironment() {
  return LoadFromEnvironment([](const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) return std::optional<std::string>{};
    return std::optional<std::string>{value};
  });
}

AppConfig LoadFromEnvironment(const EnvironmentReader& reader) {
  AppConfig config;
  AssignString(reader, "MONGODB_URI", &config.mongodb_uri);
  AssignString(reader, "BLOG_DATABASE", &config.database_name);
  AssignInteger(reader, "MONGO_MIN_POOL_SIZE", 0, 1000,
                &config.mongo_min_pool_size);
  AssignInteger(reader, "MONGO_MAX_POOL_SIZE", 1, 1000,
                &config.mongo_max_pool_size);
  AssignInteger(reader, "MONGO_SERVER_SELECTION_TIMEOUT_MS", 100, 300'000,
                &config.mongo_server_selection_timeout_ms);
  AssignInteger(reader, "MONGO_CONNECT_TIMEOUT_MS", 100, 300'000,
                &config.mongo_connect_timeout_ms);
  AssignString(reader, "HTTP_HOST", &config.http_host);
  AssignString(reader, "GRPC_ADDRESS", &config.grpc_address);
  AssignString(reader, "GRPC_METRICS_HOST", &config.grpc_metrics_host);
  AssignInteger(reader, "PORT", 1, 65535, &config.http_port);
  AssignInteger(reader, "GRPC_METRICS_PORT", 1, 65535,
                &config.grpc_metrics_port);
  AssignInteger(reader, "CACHE_CAPACITY", 0, 1'000'000,
                &config.cache_capacity);
  AssignInteger(reader, "CACHE_TTL_SECONDS", 1, 86'400,
                &config.cache_ttl_seconds);
  AssignInteger(reader, "HTTP_READ_TIMEOUT_SECONDS", 1, 3600,
                &config.http_read_timeout_seconds);
  AssignInteger(reader, "HTTP_WRITE_TIMEOUT_SECONDS", 1, 3600,
                &config.http_write_timeout_seconds);
  AssignInteger(reader, "HTTP_KEEP_ALIVE_TIMEOUT_SECONDS", 1, 3600,
                &config.http_keep_alive_timeout_seconds);
  AssignInteger(reader, "HTTP_KEEP_ALIVE_MAX_COUNT", 1, 10'000,
                &config.http_keep_alive_max_count);
  AssignInteger(reader, "SHUTDOWN_GRACE_SECONDS", 1, 300,
                &config.shutdown_grace_seconds);
  Validate(config);
  return config;
}

void ApplyHttpCommandLine(AppConfig& config, int argc, char** argv) {
  if (argc > 1) config.http_host = argv[1];
  if (argc > 2) config.http_port = ParseInteger("HTTP port", argv[2], 1, 65535);
  if (argc > 3) config.mongodb_uri = argv[3];
  if (argc > 4) {
    throw std::invalid_argument(
        "usage: blog_http_server [host] [port] [mongodb_uri]");
  }
  Validate(config);
}

void ApplyGrpcCommandLine(AppConfig& config, int argc, char** argv) {
  if (argc > 1) config.grpc_address = argv[1];
  if (argc > 2) config.mongodb_uri = argv[2];
  if (argc > 3) {
    throw std::invalid_argument(
        "usage: blog_grpc_server [address] [mongodb_uri]");
  }
  Validate(config);
}

void Validate(const AppConfig& config) {
  if (config.mongodb_uri.empty()) {
    throw std::invalid_argument("MONGODB_URI must not be empty");
  }
  if (config.database_name.empty()) {
    throw std::invalid_argument("BLOG_DATABASE must not be empty");
  }
  if (config.http_host.empty()) {
    throw std::invalid_argument("HTTP_HOST must not be empty");
  }
  if (config.http_port < 1 || config.http_port > 65535) {
    throw std::invalid_argument("PORT must be between 1 and 65535");
  }
  if (config.grpc_address.empty() ||
      config.grpc_address.find(':') == std::string::npos) {
    throw std::invalid_argument("GRPC_ADDRESS must contain a host and port");
  }
  if (config.grpc_metrics_host.empty()) {
    throw std::invalid_argument("GRPC_METRICS_HOST must not be empty");
  }
  if (config.grpc_metrics_port < 1 || config.grpc_metrics_port > 65535) {
    throw std::invalid_argument("GRPC_METRICS_PORT must be between 1 and 65535");
  }
  if (config.cache_capacity < 0) {
    throw std::invalid_argument("CACHE_CAPACITY must not be negative");
  }
  if (config.mongo_min_pool_size > config.mongo_max_pool_size) {
    throw std::invalid_argument(
        "MONGO_MIN_POOL_SIZE must not exceed MONGO_MAX_POOL_SIZE");
  }
}

std::string MongoConnectionString(const AppConfig& config) {
  std::string uri = config.mongodb_uri;
  const auto append = [&uri](const std::string& name, int value) {
    if (uri.find(name + "=") != std::string::npos) return;
    uri += uri.find('?') == std::string::npos ? '?' : '&';
    uri += name + "=" + std::to_string(value);
  };
  append("minPoolSize", config.mongo_min_pool_size);
  append("maxPoolSize", config.mongo_max_pool_size);
  append("serverSelectionTimeoutMS", config.mongo_server_selection_timeout_ms);
  append("connectTimeoutMS", config.mongo_connect_timeout_ms);
  return uri;
}

}  // namespace blog::config
