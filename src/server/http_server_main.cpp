#include <iostream>
#include <string>

#include "db/blog_repository.h"
#include "server/http_app.h"

int main(int argc, char** argv) {
  const std::string listen_host = argc > 1 ? argv[1] : std::string("0.0.0.0");
  const int listen_port = argc > 2 ? std::stoi(argv[2]) : 5000;
  const std::string mongodb_uri =
      argc > 3 ? argv[3] : std::string("mongodb://localhost:27017");

  try {
    blog::db::BlogRepository repo(mongodb_uri, "blog");

    httplib::Server server;
    blog::server::ConfigureHttpServer(server, repo);

    std::cout << "HTTP Blog server listening on " << listen_host << ":"
              << listen_port << std::endl;
    if (!server.listen(listen_host, listen_port)) {
      std::cerr << "Server failed to start listening" << std::endl;
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
