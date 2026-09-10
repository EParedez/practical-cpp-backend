#include "db/blog_repository.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;

namespace blog::db {

BlogRepository::BlogRepository(const std::string& connection_string,
                               const std::string& db_name)
    : db_name_(db_name) {
  // The driver instance must exist before any pool is constructed.
  Instance();
  pool_ = std::make_unique<mongocxx::pool>(mongocxx::uri{connection_string});
}

bool BlogRepository::CreateUser(const model::User& user) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    bsoncxx::builder::basic::array profiles;
    for (const auto& profile : user.profiles) {
      profiles.append(make_document(
          kvp("platform", profile.first), kvp("handle", profile.second)));
    }

    auto result = collection.insert_one(make_document(
        kvp("username", user.username),
        kvp("email", user.email),
        kvp("password", user.password),
        kvp("profiles", profiles.view())));
    return result && result->result().inserted_count() == 1;
  } catch (const mongocxx::exception& e) {
    std::cerr << "CreateUser error: " << e.what() << std::endl;
    return false;
  }
}

std::optional<model::User> BlogRepository::FindUserByUsername(
    const std::string& username) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto maybe = collection.find_one(filter.view());
    if (!maybe) return std::nullopt;

    auto view = maybe->view();
    model::User user;
    user.username = username;
    if (view["email"]) user.email = std::string(view["email"].get_string().value);
    if (view["password"]) user.password = std::string(view["password"].get_string().value);
    return user;
  } catch (const mongocxx::exception& e) {
    std::cerr << "FindUserByUsername error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool BlogRepository::UpdateUserEmail(const std::string& username,
                                     const std::string& email) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto update = make_document(
        kvp("$set", make_document(kvp("email", email))));

    auto result = collection.update_one(filter.view(), update.view());
    return result && result->modified_count() == 1;
  } catch (const mongocxx::exception& e) {
    std::cerr << "UpdateUserEmail error: " << e.what() << std::endl;
    return false;
  }
}

bool BlogRepository::DeleteUser(const std::string& username) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto result = collection.delete_one(filter.view());
    return result && result->deleted_count() == 1;
  } catch (const mongocxx::exception& e) {
    std::cerr << "DeleteUser error: " << e.what() << std::endl;
    return false;
  }
}

std::string BlogRepository::AddPost(const model::Post& post) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    bsoncxx::builder::basic::array tags;
    for (const auto& tag : post.tags) tags.append(tag);

    bsoncxx::builder::basic::array comments;
    for (const auto& comment : post.comments) {
      comments.append(make_document(
          kvp("user", comment.user),
          kvp("content", comment.content),
          kvp("timestamp", comment.timestamp)));
    }

    auto result = collection.insert_one(make_document(
        kvp("title", post.title),
        kvp("author", post.author),
        kvp("content", post.content),
        kvp("tags", tags.view()),
        kvp("published_date", post.published_date),
        kvp("comments", comments.view())));
    if (!result) return "";

    auto id_view = result->inserted_id();
    if (id_view.type() != bsoncxx::type::k_oid) return "";
    return id_view.get_oid().value.to_string();
  } catch (const mongocxx::exception& e) {
    std::cerr << "AddPost error: " << e.what() << std::endl;
    return "";
  }
}

std::optional<model::Post> BlogRepository::FindPostById(const std::string& id) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{id}));
    auto maybe = collection.find_one(filter.view());
    if (!maybe) return std::nullopt;

    return DocumentToPost(maybe->view());
  } catch (const mongocxx::exception& e) {
    std::cerr << "FindPostById error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool BlogRepository::UpdatePost(const model::Post& post) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{post.id}));

    bsoncxx::builder::basic::array tags;
    for (const auto& tag : post.tags) tags.append(tag);

    auto update = make_document(
        kvp("$set", make_document(
                        kvp("title", post.title),
                        kvp("content", post.content),
                        kvp("tags", tags.view()))));

    auto result = collection.update_one(filter.view(), update.view());
    return result && result->modified_count() == 1;
  } catch (const mongocxx::exception& e) {
    std::cerr << "UpdatePost error: " << e.what() << std::endl;
    return false;
  }
}

bool BlogRepository::DeletePost(const std::string& id) {
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{id}));
    auto result = collection.delete_one(filter.view());
    return result && result->deleted_count() == 1;
  } catch (const mongocxx::exception& e) {
    std::cerr << "DeletePost error: " << e.what() << std::endl;
    return false;
  }
}

std::vector<model::Post> BlogRepository::GetAllPosts() {
  std::vector<model::Post> posts;
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto cursor = collection.find({});
    for (const auto& doc : cursor) {
      posts.push_back(DocumentToPost(doc));
    }
  } catch (const mongocxx::exception& e) {
    std::cerr << "GetAllPosts error: " << e.what() << std::endl;
  }
  return posts;
}

std::vector<std::pair<std::string, int>> BlogRepository::CountPostsPerAuthor() {
  std::vector<std::pair<std::string, int>> result;
  try {
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto group_spec = make_document(
        kvp("_id", "$author"),
        kvp("count", make_document(kvp("$sum", 1))));

    mongocxx::pipeline pipeline;
    pipeline.group(group_spec.view());

    auto cursor = collection.aggregate(pipeline);
    for (const auto& doc : cursor) {
      auto view = doc;
      std::string author;
      int count = 0;
      if (view["_id"] && view["_id"].type() == bsoncxx::type::k_string) {
        author = std::string(view["_id"].get_string().value);
      }
      if (view["count"]) count = view["count"].get_int32().value;
      result.emplace_back(author, count);
    }
  } catch (const mongocxx::exception& e) {
    std::cerr << "CountPostsPerAuthor error: " << e.what() << std::endl;
  }
  return result;
}

model::Post BlogRepository::DocumentToPost(const bsoncxx::document::view& view) {
  model::Post post;
  if (view["_id"]) post.id = view["_id"].get_oid().value.to_string();
  if (view["title"]) post.title = std::string(view["title"].get_string().value);
  if (view["author"]) post.author = std::string(view["author"].get_string().value);
  if (view["content"]) post.content = std::string(view["content"].get_string().value);
  if (view["published_date"]) {
    post.published_date = std::string(view["published_date"].get_string().value);
  }
  if (view["tags"]) {
    for (const auto& tag : view["tags"].get_array().value) {
      post.tags.emplace_back(tag.get_string().value);
    }
  }
  if (view["comments"]) {
    for (const auto& c : view["comments"].get_array().value) {
      model::Comment comment;
      if (c["user"]) comment.user = std::string(c["user"].get_string().value);
      if (c["content"]) comment.content = std::string(c["content"].get_string().value);
      if (c["timestamp"]) {
        comment.timestamp = std::string(c["timestamp"].get_string().value);
      }
      post.comments.push_back(comment);
    }
  }
  return post;
}

}  // namespace blog::db
