#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <bsoncxx/document/value.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>

#include "model/blog_models.h"

namespace blog::db {
class BlogRepository {
 public:
  explicit BlogRepository(const std::string& connection_string,
                          const std::string& db_name = "blog");

  // Users
  bool CreateUser(const model::User& user);
  std::optional<model::User> FindUserByUsername(const std::string& username);
  bool UpdateUserEmail(const std::string& username, const std::string& email);
  bool DeleteUser(const std::string& username);

  // Posts
  std::string AddPost(const model::Post& post);
  std::optional<model::Post> FindPostById(const std::string& id);
  bool UpdatePost(const model::Post& post);
  bool DeletePost(const std::string& id);
  std::vector<model::Post> GetAllPosts();
  // Queries
  std::vector<std::pair<std::string, int>> CountPostsPerAuthor();

  // Exposes the connection pool (used by tests and tools).
  mongocxx::pool& pool() { return *pool_; }

  // mongocxx::instance is a process-wide singleton; share one across all repos.
  static mongocxx::instance& Instance() {
    static mongocxx::instance inst{};
    return inst;
  }

 private:
  model::Post DocumentToPost(const bsoncxx::document::view& view);

  std::unique_ptr<mongocxx::pool> pool_;
  std::string db_name_;
};

}  // namespace blog::db
