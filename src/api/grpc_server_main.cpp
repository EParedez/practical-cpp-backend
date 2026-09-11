#include <grpcpp/grpcpp.h>

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "api/blog_service_impl.h"
#include "auth/auth.h"
#include "common/observability.h"
#include "config/app_config.h"
#include "db/blog_repository.h"
#include "server/httplib.h"

namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void HandleSignal(int) { shutdown_requested = 1; }

std::string ReadFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read TLS file: " + path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

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
    blog::config::ApplyGrpcCommandLine(config, argc, argv);
    blog::db::BlogRepository repo(blog::config::MongoConnectionString(config),
                                  config.database_name);
    const auto schema = repo.InitializeSchema();
    if (!schema.ok()) {
      throw std::runtime_error("database schema initialization failed: " + schema.message);
    }

    blog::security::Authenticator authenticator(config.auth_required, Credentials(config));
    blog::security::FixedWindowRateLimiter rate_limiter(
        static_cast<std::size_t>(config.rate_limit_per_minute));
    blog::api::BlogServiceImpl service(repo, static_cast<std::size_t>(config.cache_capacity),
                                       std::chrono::seconds(config.cache_ttl_seconds),
                                       &authenticator, &rate_limiter,
                                       config.protect_read_endpoints);
    grpc::ServerBuilder builder;
    std::shared_ptr<grpc::ServerCredentials> server_credentials;
    if (!config.tls_certificate_file.empty()) {
      grpc::SslServerCredentialsOptions tls;
      tls.pem_key_cert_pairs.push_back(
          {ReadFile(config.tls_private_key_file), ReadFile(config.tls_certificate_file)});
      server_credentials = grpc::SslServerCredentials(tls);
    } else {
      server_credentials = grpc::InsecureServerCredentials();
    }
    builder.AddListeningPort(config.grpc_address, server_credentials);
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
      blog::observability::Log("error", "grpc_server_bind_failed",
                               {{"address", config.grpc_address}});
      return 1;
    }

    httplib::Server metrics_server;
    metrics_server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
      response.set_content("ok", "text/plain");
    });
    metrics_server.Get("/metrics", [](const httplib::Request&, httplib::Response& response) {
      response.set_content(blog::observability::Metrics::Instance().ToPrometheus(),
                           "text/plain; version=0.0.4");
    });
    if (!metrics_server.bind_to_port(config.grpc_metrics_host, config.grpc_metrics_port)) {
      blog::observability::Log(
          "error", "grpc_metrics_bind_failed",
          {{"host", config.grpc_metrics_host}, {"port", std::to_string(config.grpc_metrics_port)}});
      server->Shutdown();
      return 1;
    }
    std::thread metrics_thread([&] { metrics_server.listen_after_bind(); });
    metrics_server.wait_until_ready();

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    blog::observability::Log("info", "grpc_server_started",
                             {{"address", config.grpc_address},
                              {"database", config.database_name},
                              {"metrics_host", config.grpc_metrics_host},
                              {"metrics_port", std::to_string(config.grpc_metrics_port)}});

    std::thread wait_thread([&] { server->Wait(); });
    while (!shutdown_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    blog::observability::Log("info", "grpc_server_shutdown_requested",
                             {{"grace_seconds", std::to_string(config.shutdown_grace_seconds)}});
    server->Shutdown(std::chrono::system_clock::now() +
                     std::chrono::seconds(config.shutdown_grace_seconds));
    metrics_server.stop();
    wait_thread.join();
    if (metrics_thread.joinable()) metrics_thread.join();
    blog::observability::Log("info", "grpc_server_stopped");
  } catch (const std::exception& e) {
    blog::observability::Log("error", "grpc_server_fatal", {{"message", e.what()}});
    return 1;
  }
  return 0;
}
