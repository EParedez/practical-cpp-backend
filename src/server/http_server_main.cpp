#include <httplib.h>

#include <iostream>
#include <string>

#include "db/blog_repository.h"
#include "model/blog_models.h"

int main(int argc, char** argv) {
  const std::string listen_host = argc > 1 ? argv[1] : std::string("0.0.0.0");
  const int listen_port = argc > 2 ? std::stoi(argv[2]) : 5000;
  const std::string mongodb_uri =
      argc > 3 ? argv[3] : std::string("mongodb://localhost:27017");

  try {
    blog::db::BlogRepository repo(mongodb_uri, "blog");

    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("ok", "text/plain");
    });

    server.Get("/posts", [&repo](const httplib::Request&, httplib::Response& res) {
      auto posts = repo.GetAllPosts();
      std::string body = "[";
      for (size_t i = 0; i < posts.size(); ++i) {
        if (i > 0) body += ",";
        body += "{\"id\":\"" + posts[i].id +
                "\",\"title\":\"" + posts[i].title +
                "\",\"author\":\"" + posts[i].author + "\"}";
      }
      body += "]";
      res.set_content(body, "application/json");
    });

    server.Post("/posts", [&repo](const httplib::Request& req, httplib::Response& res) {
      std::string title, author, content;
      auto t = req.get_param_value("title");
      auto a = req.get_param_value("author");
      auto c = req.get_param_value("content");
      if (t.empty() || a.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"title and author required\"}",
                        "application/json");
        return;
      }
      blog::model::Post post;
      post.title = t;
      post.author = a;
      post.content = c;
      std::string id = repo.AddPost(post);
      if (id.empty()) {
        res.status = 500;
        res.set_content("{\"error\":\"failed to insert post\"}",
                        "application/json");
        return;
      }
      res.status = 201;
      res.set_content("{\"id\":\"" + id + "\"}", "application/json");
    });

    server.Get("/posts/:id", [&repo](const httplib::Request& req, httplib::Response& res) {
      auto maybe = repo.FindPostById(req.path_params.at("id"));
      if (!maybe) {
        res.status = 404;
        res.set_content("{\"error\":\"post not found\"}", "application/json");
        return;
      }
      res.set_content("{\"id\":\"" + maybe->id + "\",\"title\":\"" + maybe->title +
                          "\",\"author\":\"" + maybe->author +
                          "\",\"content\":\"" + maybe->content + "\"}",
                      "application/json");
    });

    server.Delete("/posts/:id", [&repo](const httplib::Request& req, httplib::Response& res) {
      if (!repo.DeletePost(req.path_params.at("id"))) {
        res.status = 404;
        res.set_content("{\"error\":\"post not found\"}", "application/json");
        return;
      }
      res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/stats/posts-per-author",
               [&repo](const httplib::Request&, httplib::Response& res) {
                 auto counts = repo.CountPostsPerAuthor();
                 std::string body = "{";
                 for (size_t i = 0; i < counts.size(); ++i) {
                   if (i > 0) body += ",";
                   body += "\"" + counts[i].first + "\":" +
                           std::to_string(counts[i].second);
                 }
                 body += "}";
                 res.set_content(body, "application/json");
               });

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
