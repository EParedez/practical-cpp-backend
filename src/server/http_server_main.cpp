#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

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
    blog::db::BlogRepository repo(config.mongodb_uri, config.database_name);

    httplib::Server server;
    blog::server::HttpServerOptions options;
    options.read_timeout_seconds = config.http_read_timeout_seconds;
    options.write_timeout_seconds = config.http_write_timeout_seconds;
    options.keep_alive_timeout_seconds =
        config.http_keep_alive_timeout_seconds;
    options.keep_alive_max_count = config.http_keep_alive_max_count;
    blog::server::ConfigureHttpServer(server, repo, options);

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
