#pragma once

#include <memory>
#include <string>

#include <bsoncxx/document/value.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>

#include "db/blog_store.h"

namespace blog::db {
class BlogRepository final : public BlogStore {
 public:
  explicit BlogRepository(const std::string& connection_string,
                          const std::string& db_name = "blog");

  // Users
  RepositoryResult<bool> CreateUser(const model::User& user) override;
  RepositoryResult<model::User> FindUserByUsername(
      const std::string& username) override;
  RepositoryResult<bool> UpdateUserEmail(const std::string& username,
                                          const std::string& email) override;
  RepositoryResult<bool> DeleteUser(const std::string& username) override;

  // Posts
  RepositoryResult<std::string> AddPost(const model::Post& post) override;
  RepositoryResult<model::Post> FindPostById(const std::string& id) override;
  RepositoryResult<bool> UpdatePost(const model::Post& post) override;
  RepositoryResult<bool> DeletePost(const std::string& id) override;
  RepositoryResult<std::vector<model::Post>> GetAllPosts(
      std::int64_t limit = 100, std::int64_t offset = 0) override;
  // Queries
  RepositoryResult<std::vector<std::pair<std::string, int>>>
  CountPostsPerAuthor() override;
  RepositoryResult<bool> Ping() override;

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
