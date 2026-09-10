#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blog::model {

struct User {
  std::string username;
  std::string email;
  std::string password;  // hashed
  std::vector<std::pair<std::string, std::string>> profiles;  // platform, handle
};

struct Comment {
  std::string user;
  std::string content;
  std::string timestamp;
};

struct Post {
  std::string id;
  std::string title;
  std::string author;
  std::string content;
  std::vector<Comment> comments;
  std::vector<std::string> tags;
  std::string published_date;
};

struct PostQuery {
  std::int64_t limit{20};
  std::int64_t offset{0};
  std::optional<std::string> author;
  std::optional<std::string> tag;
  std::optional<std::string> published_from;
  std::optional<std::string> published_to;
};

}  // namespace blog::model
