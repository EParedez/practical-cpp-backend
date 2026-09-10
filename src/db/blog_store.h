#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/blog_models.h"

namespace blog::db {

enum class RepositoryError {
  kNone,
  kInvalidArgument,
  kNotFound,
  kConflict,
  kUnavailable,
  kInternal,
};

template <typename T>
struct RepositoryResult {
  std::optional<T> value;
  RepositoryError error{RepositoryError::kNone};
  std::string message;

  [[nodiscard]] bool ok() const {
    return error == RepositoryError::kNone && value.has_value();
  }

  static RepositoryResult Success(T result) {
    RepositoryResult response;
    response.value = std::move(result);
    return response;
  }

  static RepositoryResult Failure(RepositoryError error_code,
                                  std::string error_message) {
    RepositoryResult response;
    response.error = error_code;
    response.message = std::move(error_message);
    return response;
  }
};

class BlogStore {
 public:
  virtual ~BlogStore() = default;

  virtual RepositoryResult<bool> CreateUser(const model::User& user) = 0;
  virtual RepositoryResult<model::User> FindUserByUsername(
      const std::string& username) = 0;
  virtual RepositoryResult<bool> UpdateUserEmail(const std::string& username,
                                                  const std::string& email) = 0;
  virtual RepositoryResult<bool> DeleteUser(const std::string& username) = 0;

  virtual RepositoryResult<std::string> AddPost(const model::Post& post) = 0;
  virtual RepositoryResult<model::Post> FindPostById(const std::string& id) = 0;
  virtual RepositoryResult<bool> UpdatePost(const model::Post& post) = 0;
  virtual RepositoryResult<bool> DeletePost(const std::string& id) = 0;
  virtual RepositoryResult<std::vector<model::Post>> GetAllPosts(
      const model::PostQuery& query = {}) = 0;
  virtual RepositoryResult<std::vector<std::pair<std::string, int>>>
  CountPostsPerAuthor() = 0;
  virtual RepositoryResult<bool> Ping() = 0;
};

}  // namespace blog::db
