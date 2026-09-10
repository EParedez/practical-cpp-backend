#pragma once

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

}  // namespace blog::model
