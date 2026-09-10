#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <unordered_map>

#include "config/app_config.h"

namespace {

blog::config::EnvironmentReader Reader(
    std::unordered_map<std::string, std::string> values) {
  return [values = std::move(values)](const std::string& name)
             -> std::optional<std::string> {
    const auto found = values.find(name);
    if (found == values.end()) return std::nullopt;
    return found->second;
  };
}

TEST(AppConfigTest, UsesDocumentedDefaults) {
  const auto config = blog::config::LoadFromEnvironment(Reader({}));
  EXPECT_EQ(config.mongodb_uri, "mongodb://localhost:27017");
  EXPECT_EQ(config.database_name, "blog");
  EXPECT_EQ(config.http_port, 5000);
  EXPECT_EQ(config.grpc_address, "0.0.0.0:50051");
  EXPECT_EQ(config.grpc_metrics_host, "0.0.0.0");
  EXPECT_EQ(config.grpc_metrics_port, 9090);
  EXPECT_EQ(config.cache_capacity, 128);
  EXPECT_EQ(config.cache_ttl_seconds, 60);
  EXPECT_EQ(config.mongo_min_pool_size, 1);
  EXPECT_EQ(config.mongo_max_pool_size, 20);
}

TEST(AppConfigTest, ReadsEnvironmentOverrides) {
  const auto config = blog::config::LoadFromEnvironment(Reader({
      {"MONGODB_URI", "mongodb://mongo:27017"},
      {"BLOG_DATABASE", "custom_blog"},
      {"HTTP_HOST", "127.0.0.1"},
      {"PORT", "8080"},
      {"GRPC_ADDRESS", "127.0.0.1:6000"},
      {"GRPC_METRICS_HOST", "127.0.0.1"},
      {"GRPC_METRICS_PORT", "9091"},
      {"CACHE_CAPACITY", "256"},
      {"CACHE_TTL_SECONDS", "120"},
      {"MONGO_MIN_POOL_SIZE", "2"},
      {"MONGO_MAX_POOL_SIZE", "40"},
      {"SHUTDOWN_GRACE_SECONDS", "15"},
  }));
  EXPECT_EQ(config.mongodb_uri, "mongodb://mongo:27017");
  EXPECT_EQ(config.database_name, "custom_blog");
  EXPECT_EQ(config.http_host, "127.0.0.1");
  EXPECT_EQ(config.http_port, 8080);
  EXPECT_EQ(config.grpc_address, "127.0.0.1:6000");
  EXPECT_EQ(config.grpc_metrics_host, "127.0.0.1");
  EXPECT_EQ(config.grpc_metrics_port, 9091);
  EXPECT_EQ(config.cache_capacity, 256);
  EXPECT_EQ(config.cache_ttl_seconds, 120);
  EXPECT_EQ(config.mongo_min_pool_size, 2);
  EXPECT_EQ(config.mongo_max_pool_size, 40);
  EXPECT_EQ(config.shutdown_grace_seconds, 15);
}

TEST(AppConfigTest, RejectsInvalidNumericEnvironmentValue) {
  EXPECT_THROW(blog::config::LoadFromEnvironment(Reader({{"PORT", "invalid"}})),
               std::invalid_argument);
  EXPECT_THROW(blog::config::LoadFromEnvironment(Reader({{"PORT", "70000"}})),
               std::invalid_argument);
  EXPECT_THROW(
      blog::config::LoadFromEnvironment(Reader({{"CACHE_CAPACITY", "-1"}})),
      std::invalid_argument);
  EXPECT_THROW(blog::config::LoadFromEnvironment(Reader({
                   {"MONGO_MIN_POOL_SIZE", "10"},
                   {"MONGO_MAX_POOL_SIZE", "5"},
               })),
               std::invalid_argument);
}

TEST(AppConfigTest, AddsMongoPoolAndTimeoutOptionsWithoutOverridingUriValues) {
  auto config = blog::config::LoadFromEnvironment(Reader({
      {"MONGODB_URI", "mongodb://localhost:27017/?maxPoolSize=7"},
      {"MONGO_MAX_POOL_SIZE", "20"},
      {"MONGO_CONNECT_TIMEOUT_MS", "2500"},
  }));
  const auto uri = blog::config::MongoConnectionString(config);
  EXPECT_NE(uri.find("maxPoolSize=7"), std::string::npos);
  EXPECT_EQ(uri.find("maxPoolSize=20"), std::string::npos);
  EXPECT_NE(uri.find("connectTimeoutMS=2500"), std::string::npos);
}

}  // namespace
