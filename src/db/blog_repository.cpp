#include "db/blog_repository.h"

#include <algorithm>
#include <cctype>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/options/find.hpp>

#include "common/observability.h"
#include "model/blog_validation.h"

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;

namespace blog::db {

namespace {

class MongoOperationMetric {
 public:
  ~MongoOperationMetric() {
    observability::Metrics::Instance().RecordMongoOperation(!succeeded_);
  }

  void Succeed() { succeeded_ = true; }

 private:
  bool succeeded_{false};
};

bool IsValidObjectId(const std::string& id) {
  return id.size() == 24 &&
         std::all_of(id.begin(), id.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

template <typename T>
RepositoryResult<T> MongoFailure(const mongocxx::exception& error) {
  if (error.code().value() == 11000) {
    return RepositoryResult<T>::Failure(RepositoryError::kConflict,
                                        "duplicate database value");
  }
  return RepositoryResult<T>::Failure(RepositoryError::kUnavailable,
                                      error.what());
}

template <typename T>
RepositoryResult<T> BsonFailure(const bsoncxx::exception& error) {
  return RepositoryResult<T>::Failure(RepositoryError::kInternal, error.what());
}

}  // namespace

BlogRepository::BlogRepository(const std::string& connection_string,
                               const std::string& db_name)
    : db_name_(db_name) {
  // The driver instance must exist before any pool is constructed.
  Instance();
  pool_ = std::make_unique<mongocxx::pool>(mongocxx::uri{connection_string});
}

RepositoryResult<bool> BlogRepository::CreateUser(const model::User& user) {
  if (user.username.empty() || user.email.empty()) {
    return RepositoryResult<bool>::Failure(
        RepositoryError::kInvalidArgument, "username and email are required");
  }
  try {
    MongoOperationMetric metric;
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
    metric.Succeed();
    if (!result || result->result().inserted_count() != 1) {
      return RepositoryResult<bool>::Failure(RepositoryError::kInternal,
                                              "user was not inserted");
    }
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
}

RepositoryResult<model::User> BlogRepository::FindUserByUsername(
    const std::string& username) {
  if (username.empty()) {
    return RepositoryResult<model::User>::Failure(
        RepositoryError::kInvalidArgument, "username is required");
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto maybe = collection.find_one(filter.view());
    metric.Succeed();
    if (!maybe) {
      return RepositoryResult<model::User>::Failure(
          RepositoryError::kNotFound, "user not found");
    }

    auto view = maybe->view();
    model::User user;
    user.username = username;
    if (view["email"]) user.email = std::string(view["email"].get_string().value);
    if (view["password"]) user.password = std::string(view["password"].get_string().value);
    return RepositoryResult<model::User>::Success(std::move(user));
  } catch (const mongocxx::exception& e) {
    return MongoFailure<model::User>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<model::User>(e);
  }
}

RepositoryResult<bool> BlogRepository::UpdateUserEmail(
    const std::string& username, const std::string& email) {
  if (username.empty() || email.empty()) {
    return RepositoryResult<bool>::Failure(
        RepositoryError::kInvalidArgument, "username and email are required");
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto update = make_document(
        kvp("$set", make_document(kvp("email", email))));

    auto result = collection.update_one(filter.view(), update.view());
    metric.Succeed();
    if (!result || result->matched_count() == 0) {
      return RepositoryResult<bool>::Failure(RepositoryError::kNotFound,
                                              "user not found");
    }
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
}

RepositoryResult<bool> BlogRepository::DeleteUser(const std::string& username) {
  if (username.empty()) {
    return RepositoryResult<bool>::Failure(RepositoryError::kInvalidArgument,
                                            "username is required");
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["users"];

    auto filter = make_document(kvp("username", username));
    auto result = collection.delete_one(filter.view());
    metric.Succeed();
    if (!result || result->deleted_count() == 0) {
      return RepositoryResult<bool>::Failure(RepositoryError::kNotFound,
                                              "user not found");
    }
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
}

RepositoryResult<std::string> BlogRepository::AddPost(
    const model::Post& post) {
  if (const auto error = model::ValidatePost(post)) {
    return RepositoryResult<std::string>::Failure(
        RepositoryError::kInvalidArgument, *error);
  }
  try {
    MongoOperationMetric metric;
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
    metric.Succeed();
    if (!result) {
      return RepositoryResult<std::string>::Failure(
          RepositoryError::kInternal, "post was not inserted");
    }

    auto id_view = result->inserted_id();
    if (id_view.type() != bsoncxx::type::k_oid) {
      return RepositoryResult<std::string>::Failure(
          RepositoryError::kInternal, "inserted post has no ObjectId");
    }
    return RepositoryResult<std::string>::Success(
        id_view.get_oid().value.to_string());
  } catch (const mongocxx::exception& e) {
    return MongoFailure<std::string>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<std::string>(e);
  }
}

RepositoryResult<model::Post> BlogRepository::FindPostById(
    const std::string& id) {
  if (!IsValidObjectId(id)) {
    return RepositoryResult<model::Post>::Failure(
        RepositoryError::kInvalidArgument, "invalid post id");
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{id}));
    auto maybe = collection.find_one(filter.view());
    metric.Succeed();
    if (!maybe) {
      return RepositoryResult<model::Post>::Failure(
          RepositoryError::kNotFound, "post not found");
    }

    return RepositoryResult<model::Post>::Success(
        DocumentToPost(maybe->view()));
  } catch (const mongocxx::exception& e) {
    return MongoFailure<model::Post>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<model::Post>(e);
  }
}

RepositoryResult<bool> BlogRepository::UpdatePost(const model::Post& post) {
  if (!IsValidObjectId(post.id)) {
    return RepositoryResult<bool>::Failure(RepositoryError::kInvalidArgument,
                                            "invalid post id");
  }
  if (const auto error = model::ValidatePost(post)) {
    return RepositoryResult<bool>::Failure(
        RepositoryError::kInvalidArgument, *error);
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{post.id}));

    bsoncxx::builder::basic::array tags;
    for (const auto& tag : post.tags) tags.append(tag);

    auto update = make_document(
        kvp("$set", make_document(
                        kvp("title", post.title),
                        kvp("author", post.author),
                        kvp("content", post.content),
                        kvp("tags", tags.view()),
                        kvp("published_date", post.published_date))));

    auto result = collection.update_one(filter.view(), update.view());
    metric.Succeed();
    if (!result || result->matched_count() == 0) {
      return RepositoryResult<bool>::Failure(RepositoryError::kNotFound,
                                              "post not found");
    }
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
}

RepositoryResult<bool> BlogRepository::DeletePost(const std::string& id) {
  if (!IsValidObjectId(id)) {
    return RepositoryResult<bool>::Failure(RepositoryError::kInvalidArgument,
                                            "invalid post id");
  }
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    auto filter = make_document(kvp("_id", bsoncxx::oid{id}));
    auto result = collection.delete_one(filter.view());
    metric.Succeed();
    if (!result || result->deleted_count() == 0) {
      return RepositoryResult<bool>::Failure(RepositoryError::kNotFound,
                                              "post not found");
    }
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
}

RepositoryResult<std::vector<model::Post>> BlogRepository::GetAllPosts(
    std::int64_t limit, std::int64_t offset) {
  if (limit < 1 || limit > 100 || offset < 0) {
    return RepositoryResult<std::vector<model::Post>>::Failure(
        RepositoryError::kInvalidArgument,
        "limit must be between 1 and 100 and offset must be non-negative");
  }
  std::vector<model::Post> posts;
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto collection = (*client)[db_name_]["posts"];

    mongocxx::options::find options;
    options.limit(limit);
    options.skip(offset);
    auto cursor = collection.find({}, options);
    for (const auto& doc : cursor) {
      posts.push_back(DocumentToPost(doc));
    }
    metric.Succeed();
    return RepositoryResult<std::vector<model::Post>>::Success(
        std::move(posts));
  } catch (const mongocxx::exception& e) {
    return MongoFailure<std::vector<model::Post>>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<std::vector<model::Post>>(e);
  }
}

RepositoryResult<std::vector<std::pair<std::string, int>>>
BlogRepository::CountPostsPerAuthor() {
  std::vector<std::pair<std::string, int>> result;
  try {
    MongoOperationMetric metric;
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
    metric.Succeed();
    return RepositoryResult<std::vector<std::pair<std::string, int>>>::Success(
        std::move(result));
  } catch (const mongocxx::exception& e) {
    return MongoFailure<std::vector<std::pair<std::string, int>>>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<std::vector<std::pair<std::string, int>>>(e);
  }
}

RepositoryResult<bool> BlogRepository::Ping() {
  try {
    MongoOperationMetric metric;
    auto client = pool_->acquire();
    auto command = make_document(kvp("ping", 1));
    (*client)[db_name_].run_command(command.view());
    metric.Succeed();
    return RepositoryResult<bool>::Success(true);
  } catch (const mongocxx::exception& e) {
    return MongoFailure<bool>(e);
  } catch (const bsoncxx::exception& e) {
    return BsonFailure<bool>(e);
  }
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
