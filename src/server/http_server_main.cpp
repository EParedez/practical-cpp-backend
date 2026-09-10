#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "cache/post_cache.h"
#include "common/observability.h"
#include "config/app_config.h"
#include "db/blog_repository.h"
#include "server/http_app.h"

namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void HandleSignal(int) { shutdown_requested = 1; }

}  // namespace

int main(int argc, char** argv) {
  try {
    auto config = blog::config::LoadFromEnvironment();
    blog::config::ApplyHttpCommandLine(config, argc, argv);
    blog::db::BlogRepository repo(blog::config::MongoConnectionString(config),
                                  config.database_name);
    const auto schema = repo.InitializeSchema();
    if (!schema.ok()) {
      throw std::runtime_error("database schema initialization failed: " +
                               schema.message);
    }

    httplib::Server server;
    blog::cache::ThreadSafeLruPostCache cache(
        static_cast<std::size_t>(config.cache_capacity),
        std::chrono::seconds(config.cache_ttl_seconds));
    blog::server::HttpServerOptions options;
    options.read_timeout_seconds = config.http_read_timeout_seconds;
    options.write_timeout_seconds = config.http_write_timeout_seconds;
    options.keep_alive_timeout_seconds =
        config.http_keep_alive_timeout_seconds;
    options.keep_alive_max_count = config.http_keep_alive_max_count;
    blog::server::ConfigureHttpServer(server, repo, options, &cache);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    if (!server.bind_to_port(config.http_host, config.http_port)) {
      blog::observability::Log(
          "error", "http_server_bind_failed",
          {{"host", config.http_host}, {"port", std::to_string(config.http_port)}});
      return 1;
    }

    std::atomic<bool> server_finished{false};
    std::atomic<bool> listen_succeeded{false};
    std::thread server_thread([&] {
      listen_succeeded = server.listen_after_bind();
      server_finished = true;
    });
    server.wait_until_ready();
    blog::observability::Log(
        "info", "http_server_started",
        {{"host", config.http_host},
         {"port", std::to_string(config.http_port)},
         {"database", config.database_name}});

    while (!shutdown_requested && !server_finished.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (shutdown_requested) {
      blog::observability::Log(
          "info", "http_server_shutdown_requested",
          {{"grace_seconds", std::to_string(config.shutdown_grace_seconds)}});
      server.stop();
    }
    if (server_thread.joinable()) server_thread.join();
    blog::observability::Log("info", "http_server_stopped");
    return listen_succeeded.load() || shutdown_requested ? 0 : 1;
  } catch (const std::exception& e) {
    blog::observability::Log("error", "http_server_fatal",
                             {{"message", e.what()}});
    return 1;
  }
}
