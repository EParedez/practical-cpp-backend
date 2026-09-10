#include <gtest/gtest.h>

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include "db/blog_store.h"
#include "server/http_app.h"

namespace {

using blog::db::RepositoryError;

class InMemoryBlogStore final : public blog::db::BlogStore {
 public:
  blog::db::RepositoryResult<bool> CreateUser(
      const blog::model::User&) override {
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<blog::model::User> FindUserByUsername(
      const std::string&) override {
    return blog::db::RepositoryResult<blog::model::User>::Failure(
        RepositoryError::kNotFound, "user not found");
  }

  blog::db::RepositoryResult<bool> UpdateUserEmail(
      const std::string&, const std::string&) override {
    return blog::db::RepositoryResult<bool>::Failure(
        RepositoryError::kNotFound, "user not found");
  }

  blog::db::RepositoryResult<bool> DeleteUser(
      const std::string&) override {
    return blog::db::RepositoryResult<bool>::Failure(
        RepositoryError::kNotFound, "user not found");
  }

  blog::db::RepositoryResult<std::string> AddPost(
      const blog::model::Post& post) override {
    if (!available.load()) return Unavailable<std::string>();
    auto stored = post;
    stored.id = kPostId;
    posts = {stored};
    return blog::db::RepositoryResult<std::string>::Success(kPostId);
  }

  blog::db::RepositoryResult<blog::model::Post> FindPostById(
      const std::string& id) override {
    if (!available.load()) return Unavailable<blog::model::Post>();
    if (id.size() != 24) {
      return blog::db::RepositoryResult<blog::model::Post>::Failure(
          RepositoryError::kInvalidArgument, "invalid post id");
    }
    const auto found = std::find_if(
        posts.begin(), posts.end(),
        [&id](const blog::model::Post& post) { return post.id == id; });
    if (found == posts.end()) {
      return blog::db::RepositoryResult<blog::model::Post>::Failure(
          RepositoryError::kNotFound, "post not found");
    }
    return blog::db::RepositoryResult<blog::model::Post>::Success(*found);
  }

  blog::db::RepositoryResult<bool> UpdatePost(
      const blog::model::Post& post) override {
    if (!available.load()) return Unavailable<bool>();
    if (post.id.size() != 24) {
      return blog::db::RepositoryResult<bool>::Failure(
          RepositoryError::kInvalidArgument, "invalid post id");
    }
    const auto found = std::find_if(
        posts.begin(), posts.end(), [&post](const blog::model::Post& candidate) {
          return candidate.id == post.id;
        });
    if (found == posts.end()) {
      return blog::db::RepositoryResult<bool>::Failure(
          RepositoryError::kNotFound, "post not found");
    }
    *found = post;
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<bool> DeletePost(
      const std::string& id) override {
    if (!available.load()) return Unavailable<bool>();
    const auto original_size = posts.size();
    posts.erase(std::remove_if(posts.begin(), posts.end(),
                               [&id](const blog::model::Post& post) {
                                 return post.id == id;
                               }),
                posts.end());
    if (posts.size() == original_size) {
      return blog::db::RepositoryResult<bool>::Failure(
          id.size() == 24 ? RepositoryError::kNotFound
                          : RepositoryError::kInvalidArgument,
          id.size() == 24 ? "post not found" : "invalid post id");
    }
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<std::vector<blog::model::Post>> GetAllPosts(
      std::int64_t limit, std::int64_t offset) override {
    if (!available.load()) {
      return Unavailable<std::vector<blog::model::Post>>();
    }
    std::vector<blog::model::Post> page;
    const auto begin = std::min<std::size_t>(
        static_cast<std::size_t>(offset), posts.size());
    const auto end = std::min<std::size_t>(
        begin + static_cast<std::size_t>(limit), posts.size());
    page.insert(page.end(), posts.begin() + begin, posts.begin() + end);
    return blog::db::RepositoryResult<std::vector<blog::model::Post>>::Success(
        std::move(page));
  }

  blog::db::RepositoryResult<std::vector<std::pair<std::string, int>>>
  CountPostsPerAuthor() override {
    if (!available.load()) {
      return Unavailable<std::vector<std::pair<std::string, int>>>();
    }
    return blog::db::RepositoryResult<
        std::vector<std::pair<std::string, int>>>::Success({{"writer", 1}});
  }

  blog::db::RepositoryResult<bool> Ping() override {
    if (!available.load()) return Unavailable<bool>();
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  template <typename T>
  static blog::db::RepositoryResult<T> Unavailable() {
    return blog::db::RepositoryResult<T>::Failure(
        RepositoryError::kUnavailable, "database unavailable");
  }

  static constexpr const char* kPostId = "0123456789abcdef01234567";
  std::atomic<bool> available{true};
  std::vector<blog::model::Post> posts;
};

class HttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    blog::server::ConfigureHttpServer(server, store);
    port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    server_thread = std::thread([this] { server.listen_after_bind(); });
    server.wait_until_ready();
    client = std::make_unique<httplib::Client>("127.0.0.1", port);
    client->set_connection_timeout(1, 0);
    client->set_read_timeout(2, 0);
    client->set_write_timeout(2, 0);
  }

  void TearDown() override {
    server.stop();
    if (server_thread.joinable()) server_thread.join();
  }

  InMemoryBlogStore store;
  httplib::Server server;
  int port{-1};
  std::thread server_thread;
  std::unique_ptr<httplib::Client> client;
};

TEST_F(HttpServerTest, CreatesAndReturnsEscapedJsonPost) {
  const std::string body =
      R"({"title":"A \"quoted\" title","author":"Jos\u00e9","content":"line 1\nline 2","tags":["cpp"],"published_date":"2026-09-10T12:00:00Z"})";
  const auto created = client->Post("/posts", body, "application/json");
  ASSERT_TRUE(created);
  EXPECT_EQ(created->status, 201);

  const auto fetched =
      client->Get(std::string("/posts/") + InMemoryBlogStore::kPostId);
  ASSERT_TRUE(fetched);
  ASSERT_EQ(fetched->status, 200);
  const auto document = bsoncxx::from_json(fetched->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["title"].get_string().value),
            "A \"quoted\" title");
  EXPECT_EQ(std::string(json["author"].get_string().value), "José");
  EXPECT_EQ(std::string(json["content"].get_string().value), "line 1\nline 2");
}

TEST_F(HttpServerTest, RejectsMalformedJsonWithStructuredError) {
  const auto result = client->Post("/posts", "{not-json", "application/json");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_FALSE(result->get_header_value("X-Request-ID").empty());
  const auto document = bsoncxx::from_json(result->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["error"]["code"].get_string().value),
            "invalid_post");
  EXPECT_TRUE(json["error"]["request_id"]);
}

TEST_F(HttpServerTest, RejectsUnsupportedContentType) {
  const auto result = client->Post("/posts", "title=x", "text/plain");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 415);
  EXPECT_NE(result->body.find("Content-Type must be application/json"),
            std::string::npos);
}

TEST_F(HttpServerTest, MapsInvalidIdentifiersToBadRequest) {
  const auto result = client->Get("/posts/not-an-object-id");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_NE(result->body.find("invalid_argument"), std::string::npos);
}

TEST_F(HttpServerTest, MapsDatabaseFailureToServiceUnavailable) {
  store.available = false;
  const auto result = client->Get("/posts?limit=10&offset=0");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 503);
  EXPECT_NE(result->body.find("database_unavailable"), std::string::npos);
}

TEST_F(HttpServerTest, ReturnsPaginatedListEnvelope) {
  const auto created = client->Post(
      "/posts", R"({"title":"first","author":"writer"})",
      "application/json");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);

  const auto result = client->Get("/posts?limit=1&offset=0");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->status, 200);
  const auto document = bsoncxx::from_json(result->body);
  const auto json = document.view();
  EXPECT_EQ(json["count"].get_int32().value, 1);
  EXPECT_EQ(json["limit"].get_int32().value, 1);
  const auto items = json["items"].get_array().value;
  EXPECT_EQ(std::distance(items.begin(), items.end()), 1);
}

TEST_F(HttpServerTest, RejectsPaginationOutsideAllowedRange) {
  const auto result = client->Get("/posts?limit=101&offset=-1");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_NE(result->body.find("invalid_pagination"), std::string::npos);
}

TEST_F(HttpServerTest, RejectsTooManyRequestHeaders) {
  httplib::Headers headers;
  for (int index = 0; index < 51; ++index) {
    headers.emplace("X-Test-" + std::to_string(index), "value");
  }
  const auto result = client->Get("/health", headers);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 431);
  EXPECT_NE(result->body.find("request_headers_too_large"), std::string::npos);
}

TEST_F(HttpServerTest, UpdatesPostWithPut) {
  const auto created = client->Post(
      "/posts", R"({"title":"before","author":"writer"})",
      "application/json");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);

  const std::string body =
      R"({"title":"after","author":"new writer","content":"updated","tags":["api"]})";
  const auto result = client->Put(
      std::string("/posts/") + InMemoryBlogStore::kPostId, body,
      "application/json");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->status, 200);
  const auto document = bsoncxx::from_json(result->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["title"].get_string().value), "after");
  EXPECT_EQ(std::string(json["author"].get_string().value), "new writer");
}

TEST_F(HttpServerTest, ReadinessDependsOnDatabase) {
  auto result = client->Get("/ready");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200);

  store.available = false;
  result = client->Get("/ready");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 503);
}

TEST_F(HttpServerTest, ExposesPrometheusMetrics) {
  const auto health = client->Get("/health");
  ASSERT_TRUE(health);
  ASSERT_EQ(health->status, 200);

  const auto metrics = client->Get("/metrics");
  ASSERT_TRUE(metrics);
  EXPECT_EQ(metrics->status, 200);
  EXPECT_NE(metrics->body.find("blog_http_requests_total"), std::string::npos);
  EXPECT_NE(metrics->body.find("blog_mongo_operations_total"),
            std::string::npos);
}

}  // namespace
