#pragma once

#include <functional>
#include <optional>
#include <string>

namespace blog::config {

using EnvironmentReader = std::function<std::optional<std::string>(const std::string&)>;

struct AppConfig {
  std::string mongodb_uri{"mongodb://localhost:27017"};
  std::string database_name{"blog"};
  int mongo_min_pool_size{1};
  int mongo_max_pool_size{20};
  int mongo_server_selection_timeout_ms{5000};
  int mongo_connect_timeout_ms{5000};
  std::string http_host{"0.0.0.0"};
  int http_port{5000};
  std::string grpc_address{"0.0.0.0:50051"};
  std::string grpc_metrics_host{"0.0.0.0"};
  int grpc_metrics_port{9090};
  int cache_capacity{128};
  int cache_ttl_seconds{60};
  int http_read_timeout_seconds{5};
  int http_write_timeout_seconds{5};
  int http_keep_alive_timeout_seconds{10};
  int http_keep_alive_max_count{20};
  int shutdown_grace_seconds{10};
  bool auth_required{false};
  bool protect_read_endpoints{false};
  std::string reader_token;
  std::string writer_token;
  std::string admin_token;
  int rate_limit_per_minute{60};
  std::string cors_allowed_origin;
  std::string tls_certificate_file;
  std::string tls_private_key_file;
};

AppConfig LoadFromEnvironment();
AppConfig LoadFromEnvironment(const EnvironmentReader& reader);
void ApplyHttpCommandLine(AppConfig& config, int argc, char** argv);
void ApplyGrpcCommandLine(AppConfig& config, int argc, char** argv);
void Validate(const AppConfig& config);
std::string MongoConnectionString(const AppConfig& config);

}  // namespace blog::config
