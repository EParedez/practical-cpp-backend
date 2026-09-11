#include <gtest/gtest.h>
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "auth/auth.h"
#include "cache/post_cache.h"
#include "db/blog_store.h"
#include "server/http_app.h"

namespace {

using blog::db::RepositoryError;

class InMemoryBlogStore final : public blog::db::BlogStore {
 public:
  blog::db::RepositoryResult<bool> CreateUser(const blog::model::User&) override {
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<blog::model::User> FindUserByUsername(const std::string&) override {
    return blog::db::RepositoryResult<blog::model::User>::Failure(RepositoryError::kNotFound,
                                                                  "user not found");
  }

  blog::db::RepositoryResult<bool> UpdateUserEmail(const std::string&,
                                                   const std::string&) override {
    return blog::db::RepositoryResult<bool>::Failure(RepositoryError::kNotFound, "user not found");
  }

  blog::db::RepositoryResult<bool> DeleteUser(const std::string&) override {
    return blog::db::RepositoryResult<bool>::Failure(RepositoryError::kNotFound, "user not found");
  }

  blog::db::RepositoryResult<std::string> AddPost(const blog::model::Post& post) override {
    if (!available.load()) return Unavailable<std::string>();
    auto stored = post;
    stored.id = kPostId;
    posts = {stored};
    return blog::db::RepositoryResult<std::string>::Success(kPostId);
  }

  blog::db::RepositoryResult<blog::model::Post> FindPostById(const std::string& id) override {
    ++find_post_calls;
    if (!available.load()) return Unavailable<blog::model::Post>();
    if (id.size() != 24) {
      return blog::db::RepositoryResult<blog::model::Post>::Failure(
          RepositoryError::kInvalidArgument, "invalid post id");
    }
    const auto found = std::find_if(posts.begin(), posts.end(),
                                    [&id](const blog::model::Post& post) { return post.id == id; });
    if (found == posts.end()) {
      return blog::db::RepositoryResult<blog::model::Post>::Failure(RepositoryError::kNotFound,
                                                                    "post not found");
    }
    return blog::db::RepositoryResult<blog::model::Post>::Success(*found);
  }

  blog::db::RepositoryResult<bool> UpdatePost(const blog::model::Post& post) override {
    if (!available.load()) return Unavailable<bool>();
    if (post.id.size() != 24) {
      return blog::db::RepositoryResult<bool>::Failure(RepositoryError::kInvalidArgument,
                                                       "invalid post id");
    }
    const auto found = std::find_if(
        posts.begin(), posts.end(),
        [&post](const blog::model::Post& candidate) { return candidate.id == post.id; });
    if (found == posts.end()) {
      return blog::db::RepositoryResult<bool>::Failure(RepositoryError::kNotFound,
                                                       "post not found");
    }
    *found = post;
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<bool> DeletePost(const std::string& id) override {
    if (!available.load()) return Unavailable<bool>();
    const auto original_size = posts.size();
    posts.erase(std::remove_if(posts.begin(), posts.end(),
                               [&id](const blog::model::Post& post) { return post.id == id; }),
                posts.end());
    if (posts.size() == original_size) {
      return blog::db::RepositoryResult<bool>::Failure(
          id.size() == 24 ? RepositoryError::kNotFound : RepositoryError::kInvalidArgument,
          id.size() == 24 ? "post not found" : "invalid post id");
    }
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  blog::db::RepositoryResult<std::vector<blog::model::Post>> GetAllPosts(
      const blog::model::PostQuery& query) override {
    if (!available.load()) {
      return Unavailable<std::vector<blog::model::Post>>();
    }
    std::vector<blog::model::Post> page;
    std::vector<blog::model::Post> filtered;
    std::copy_if(posts.begin(), posts.end(), std::back_inserter(filtered),
                 [&query](const blog::model::Post& post) {
                   const bool author_matches = !query.author || post.author == *query.author;
                   const bool tag_matches =
                       !query.tag ||
                       std::find(post.tags.begin(), post.tags.end(), *query.tag) != post.tags.end();
                   return author_matches && tag_matches;
                 });
    const auto begin =
        std::min<std::size_t>(static_cast<std::size_t>(query.offset), filtered.size());
    const auto end =
        std::min<std::size_t>(begin + static_cast<std::size_t>(query.limit), filtered.size());
    page.insert(page.end(), filtered.begin() + begin, filtered.begin() + end);
    return blog::db::RepositoryResult<std::vector<blog::model::Post>>::Success(std::move(page));
  }

  blog::db::RepositoryResult<std::vector<std::pair<std::string, int>>> CountPostsPerAuthor()
      override {
    if (!available.load()) {
      return Unavailable<std::vector<std::pair<std::string, int>>>();
    }
    return blog::db::RepositoryResult<std::vector<std::pair<std::string, int>>>::Success(
        {{"writer", 1}});
  }

  blog::db::RepositoryResult<bool> Ping() override {
    if (!available.load()) return Unavailable<bool>();
    return blog::db::RepositoryResult<bool>::Success(true);
  }

  template <typename T>
  static blog::db::RepositoryResult<T> Unavailable() {
    return blog::db::RepositoryResult<T>::Failure(RepositoryError::kUnavailable,
                                                  "database unavailable");
  }

  static constexpr const char* kPostId = "0123456789abcdef01234567";
  std::atomic<bool> available{true};
  std::atomic<int> find_post_calls{0};
  std::vector<blog::model::Post> posts;
};

class HttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    blog::server::HttpServerOptions options;
    options.authenticator = &authenticator;
    options.rate_limiter = &rate_limiter;
    blog::server::ConfigureHttpServer(server, store, options, &cache);
    port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    server_thread = std::thread([this] { server.listen_after_bind(); });
    server.wait_until_ready();
    client = std::make_unique<httplib::Client>("127.0.0.1", port);
    client->set_connection_timeout(1, 0);
    client->set_read_timeout(2, 0);
    client->set_write_timeout(2, 0);
    client->set_default_headers({{"Authorization", std::string("Bearer ") + kWriterToken}});
  }

  void TearDown() override {
    server.stop();
    if (server_thread.joinable()) server_thread.join();
  }

  InMemoryBlogStore store;
  static constexpr const char* kReaderToken = "reader-token-with-at-least-32-characters";
  static constexpr const char* kWriterToken = "writer-token-with-at-least-32-characters";
  blog::security::Authenticator authenticator{
      true,
      {{"reader", kReaderToken, blog::security::Role::kReader},
       {"writer", kWriterToken, blog::security::Role::kWriter}}};
  blog::security::FixedWindowRateLimiter rate_limiter{1000};
  blog::cache::ThreadSafeLruPostCache cache{16, std::chrono::seconds(60)};
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

  const auto fetched = client->Get(std::string("/posts/") + InMemoryBlogStore::kPostId);
  ASSERT_TRUE(fetched);
  ASSERT_EQ(fetched->status, 200);
  const auto document = bsoncxx::from_json(fetched->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["title"].get_string().value), "A \"quoted\" title");
  EXPECT_EQ(std::string(json["author"].get_string().value), "José");
  EXPECT_EQ(std::string(json["content"].get_string().value), "line 1\nline 2");
}

TEST_F(HttpServerTest, RejectsAnonymousAndInsufficientRolesForMutation) {
  {
    httplib::Client anonymous("127.0.0.1", port);
    auto missing =
        anonymous.Post("/posts", R"({"title":"blocked","author":"anonymous"})", "application/json");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 401);
    EXPECT_EQ(missing->get_header_value("WWW-Authenticate"), "Bearer");
  }
  {
    httplib::Client reader("127.0.0.1", port);
    reader.set_default_headers({{"Authorization", std::string("Bearer ") + kReaderToken}});
    auto forbidden =
        reader.Post("/posts", R"({"title":"blocked","author":"reader"})", "application/json");
    ASSERT_TRUE(forbidden);
    EXPECT_EQ(forbidden->status, 403);
  }
}

TEST_F(HttpServerTest, AddsSecurityHeadersAndRejectsUnknownOrigins) {
  const auto result = client->Get("/health");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_NE(result->get_header_value("Content-Security-Policy").find("default-src 'none'"),
            std::string::npos);

  httplib::Headers origin{{"Origin", "https://untrusted.example"}};
  const auto denied = client->Get("/posts", origin);
  ASSERT_TRUE(denied);
  EXPECT_EQ(denied->status, 403);
}

TEST_F(HttpServerTest, RejectsMalformedJsonWithStructuredError) {
  const auto result = client->Post("/posts", "{not-json", "application/json");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_FALSE(result->get_header_value("X-Request-ID").empty());
  const auto document = bsoncxx::from_json(result->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["error"]["code"].get_string().value), "invalid_post");
  EXPECT_TRUE(json["error"]["request_id"]);
}

TEST_F(HttpServerTest, RejectsUnsupportedContentType) {
  const auto result = client->Post("/posts", "title=x", "text/plain");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 415);
  EXPECT_NE(result->body.find("Content-Type must be application/json"), std::string::npos);
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
  const auto created =
      client->Post("/posts", R"({"title":"first","author":"writer"})", "application/json");
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

TEST_F(HttpServerTest, FiltersPostsByAuthorAndTag) {
  auto cpp = blog::model::Post{};
  cpp.id = InMemoryBlogStore::kPostId;
  cpp.title = "C++";
  cpp.author = "alice";
  cpp.tags = {"cpp"};
  auto other = cpp;
  other.id = "abcdef0123456789abcdef01";
  other.author = "bob";
  other.tags = {"other"};
  store.posts = {cpp, other};

  const auto result = client->Get("/posts?author=alice&tag=cpp");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->status, 200);
  const auto document = bsoncxx::from_json(result->body);
  EXPECT_EQ(document.view()["count"].get_int32().value, 1);
  EXPECT_NE(result->body.find("alice"), std::string::npos);
  EXPECT_EQ(result->body.find("bob"), std::string::npos);
}

TEST_F(HttpServerTest, RejectsInvalidPublicationDateFilter) {
  const auto result = client->Get("/posts?published_from=not-a-date");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_NE(result->body.find("invalid_query"), std::string::npos);
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
  const auto created =
      client->Post("/posts", R"({"title":"before","author":"writer"})", "application/json");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);

  const std::string body =
      R"({"title":"after","author":"new writer","content":"updated","tags":["api"]})";
  const auto result =
      client->Put(std::string("/posts/") + InMemoryBlogStore::kPostId, body, "application/json");
  ASSERT_TRUE(result);
  ASSERT_EQ(result->status, 200);
  const auto document = bsoncxx::from_json(result->body);
  const auto json = document.view();
  EXPECT_EQ(std::string(json["title"].get_string().value), "after");
  EXPECT_EQ(std::string(json["author"].get_string().value), "new writer");
}

TEST_F(HttpServerTest, CachesReadsAndRefreshesAfterUpdate) {
  const auto created =
      client->Post("/posts", R"({"title":"before","author":"writer"})", "application/json");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);
  const auto path = std::string("/posts/") + InMemoryBlogStore::kPostId;

  ASSERT_EQ(client->Get(path)->status, 200);
  ASSERT_EQ(client->Get(path)->status, 200);
  EXPECT_EQ(store.find_post_calls.load(), 0);

  const auto updated =
      client->Put(path, R"({"title":"after","author":"writer"})", "application/json");
  ASSERT_TRUE(updated);
  ASSERT_EQ(updated->status, 200);
  const auto fetched = client->Get(path);
  ASSERT_TRUE(fetched);
  EXPECT_NE(fetched->body.find("after"), std::string::npos);
  EXPECT_EQ(store.find_post_calls.load(), 0);
}

TEST_F(HttpServerTest, InvalidatesCachedPostAfterDelete) {
  ASSERT_EQ(
      client->Post("/posts", R"({"title":"cached","author":"writer"})", "application/json")->status,
      201);
  const auto path = std::string("/posts/") + InMemoryBlogStore::kPostId;
  ASSERT_EQ(client->Get(path)->status, 200);
  ASSERT_EQ(client->Delete(path)->status, 200);
  const auto missing = client->Get(path);
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);
  EXPECT_EQ(store.find_post_calls.load(), 1);
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
  EXPECT_NE(metrics->body.find("blog_mongo_operations_total"), std::string::npos);
}

}  // namespace
