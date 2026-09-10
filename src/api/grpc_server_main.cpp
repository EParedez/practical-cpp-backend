#include <grpcpp/grpcpp.h>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "api/blog_service_impl.h"
#include "common/observability.h"
#include "config/app_config.h"
#include "db/blog_repository.h"
#include "server/httplib.h"

namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void HandleSignal(int) { shutdown_requested = 1; }

}  // namespace

int main(int argc, char** argv) {
  try {
    auto config = blog::config::LoadFromEnvironment();
    blog::config::ApplyGrpcCommandLine(config, argc, argv);
    blog::db::BlogRepository repo(config.mongodb_uri, config.database_name);

    blog::api::BlogServiceImpl service(repo, config.cache_capacity);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(config.grpc_address,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
      blog::observability::Log(
          "error", "grpc_server_bind_failed",
          {{"address", config.grpc_address}});
      return 1;
    }

    httplib::Server metrics_server;
    metrics_server.Get("/health", [](const httplib::Request&,
                                      httplib::Response& response) {
      response.set_content("ok", "text/plain");
    });
    metrics_server.Get("/metrics", [](const httplib::Request&,
                                       httplib::Response& response) {
      response.set_content(
          blog::observability::Metrics::Instance().ToPrometheus(),
          "text/plain; version=0.0.4");
    });
    if (!metrics_server.bind_to_port(config.grpc_metrics_host,
                                     config.grpc_metrics_port)) {
      blog::observability::Log(
          "error", "grpc_metrics_bind_failed",
          {{"host", config.grpc_metrics_host},
           {"port", std::to_string(config.grpc_metrics_port)}});
      server->Shutdown();
      return 1;
    }
    std::thread metrics_thread(
        [&] { metrics_server.listen_after_bind(); });
    metrics_server.wait_until_ready();

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    blog::observability::Log(
        "info", "grpc_server_started",
        {{"address", config.grpc_address},
         {"database", config.database_name},
         {"metrics_host", config.grpc_metrics_host},
         {"metrics_port", std::to_string(config.grpc_metrics_port)}});

    std::thread wait_thread([&] { server->Wait(); });
    while (!shutdown_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    blog::observability::Log(
        "info", "grpc_server_shutdown_requested",
        {{"grace_seconds", std::to_string(config.shutdown_grace_seconds)}});
    server->Shutdown(std::chrono::system_clock::now() +
                     std::chrono::seconds(config.shutdown_grace_seconds));
    metrics_server.stop();
    wait_thread.join();
    if (metrics_thread.joinable()) metrics_thread.join();
    blog::observability::Log("info", "grpc_server_stopped");
  } catch (const std::exception& e) {
    blog::observability::Log("error", "grpc_server_fatal",
                             {{"message", e.what()}});
    return 1;
  }
  return 0;
}
