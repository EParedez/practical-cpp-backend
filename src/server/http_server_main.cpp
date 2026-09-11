#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "auth/auth.h"
#include "cache/post_cache.h"
#include "common/observability.h"
#include "config/app_config.h"
#include "db/blog_repository.h"
#include "server/http_app.h"

namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void HandleSignal(int) { shutdown_requested = 1; }

std::vector<blog::security::TokenCredential> Credentials(const blog::config::AppConfig& config) {
  std::vector<blog::security::TokenCredential> values;
  if (!config.reader_token.empty())
    values.push_back({"reader", config.reader_token, blog::security::Role::kReader});
  if (!config.writer_token.empty())
    values.push_back({"writer", config.writer_token, blog::security::Role::kWriter});
  if (!config.admin_token.empty())
    values.push_back({"admin", config.admin_token, blog::security::Role::kAdmin});
  return values;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto config = blog::config::LoadFromEnvironment();
    blog::config::ApplyHttpCommandLine(config, argc, argv);
    blog::db::BlogRepository repo(blog::config::MongoConnectionString(config),
                                  config.database_name);
    const auto schema = repo.InitializeSchema();
    if (!schema.ok()) {
      throw std::runtime_error("database schema initialization failed: " + schema.message);
    }

    std::unique_ptr<httplib::Server> server;
    if (!config.tls_certificate_file.empty()) {
      auto tls_server = std::make_unique<httplib::SSLServer>(config.tls_certificate_file.c_str(),
                                                             config.tls_private_key_file.c_str());
      if (!tls_server->is_valid()) {
        throw std::runtime_error("HTTP TLS certificate or private key is invalid");
      }
      server = std::move(tls_server);
    } else {
      server = std::make_unique<httplib::Server>();
    }
    blog::cache::ThreadSafeLruPostCache cache(static_cast<std::size_t>(config.cache_capacity),
                                              std::chrono::seconds(config.cache_ttl_seconds));
    blog::server::HttpServerOptions options;
    options.read_timeout_seconds = config.http_read_timeout_seconds;
    options.write_timeout_seconds = config.http_write_timeout_seconds;
    options.keep_alive_timeout_seconds = config.http_keep_alive_timeout_seconds;
    options.keep_alive_max_count = config.http_keep_alive_max_count;
    blog::security::Authenticator authenticator(config.auth_required, Credentials(config));
    blog::security::FixedWindowRateLimiter rate_limiter(
        static_cast<std::size_t>(config.rate_limit_per_minute));
    options.protect_read_endpoints = config.protect_read_endpoints;
    options.cors_allowed_origin = config.cors_allowed_origin;
    options.authenticator = &authenticator;
    options.rate_limiter = &rate_limiter;
    blog::server::ConfigureHttpServer(*server, repo, options, &cache);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    if (!server->bind_to_port(config.http_host, config.http_port)) {
      blog::observability::Log(
          "error", "http_server_bind_failed",
          {{"host", config.http_host}, {"port", std::to_string(config.http_port)}});
      return 1;
    }

    std::atomic<bool> server_finished{false};
    std::atomic<bool> listen_succeeded{false};
    std::thread server_thread([&] {
      listen_succeeded = server->listen_after_bind();
      server_finished = true;
    });
    server->wait_until_ready();
    blog::observability::Log("info", "http_server_started",
                             {{"host", config.http_host},
                              {"port", std::to_string(config.http_port)},
                              {"database", config.database_name}});

    while (!shutdown_requested && !server_finished.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (shutdown_requested) {
      blog::observability::Log("info", "http_server_shutdown_requested",
                               {{"grace_seconds", std::to_string(config.shutdown_grace_seconds)}});
      server->stop();
    }
    if (server_thread.joinable()) server_thread.join();
    blog::observability::Log("info", "http_server_stopped");
    return listen_succeeded.load() || shutdown_requested ? 0 : 1;
  } catch (const std::exception& e) {
    blog::observability::Log("error", "http_server_fatal", {{"message", e.what()}});
    return 1;
  }
}
