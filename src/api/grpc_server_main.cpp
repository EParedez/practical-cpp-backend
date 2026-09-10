#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

#include "api/blog_service_impl.h"
#include "db/blog_repository.h"

int main(int argc, char** argv) {
  const std::string server_address =
      argc > 1 ? argv[1] : std::string("0.0.0.0:50051");
  const std::string mongodb_uri =
      argc > 2 ? argv[2] : std::string("mongodb://localhost:27017");

  try {
    blog::db::BlogRepository repo(mongodb_uri, "blog");

    blog::api::BlogServiceImpl service(repo);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "gRPC Blog server listening on " << server_address << std::endl;
    server->Wait();
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
