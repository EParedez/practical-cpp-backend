#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "api/blog_service_impl.h"
#include "db/blog_store.h"

namespace {

class ServiceTestStore final : public blog::db::BlogStore {
 public:
  template <typename T>
  blog::db::RepositoryResult<T> Failure() const {
    return blog::db::RepositoryResult<T>::Failure(error, "store failure");
  }

  blog::db::RepositoryResult<bool> CreateUser(
      const blog::model::User&) override {
    return Failure<bool>();
  }

  blog::db::RepositoryResult<blog::model::User> FindUserByUsername(
      const std::string&) override {
    return Failure<blog::model::User>();
  }

  blog::db::RepositoryResult<bool> UpdateUserEmail(
      const std::string&, const std::string&) override {
    return Failure<bool>();
  }

  blog::db::RepositoryResult<bool> DeleteUser(
      const std::string&) override {
    return Failure<bool>();
  }

  blog::db::RepositoryResult<std::string> AddPost(
      const blog::model::Post&) override {
    return Failure<std::string>();
  }

  blog::db::RepositoryResult<blog::model::Post> FindPostById(
      const std::string&) override {
    if (error != blog::db::RepositoryError::kNone) {
      return Failure<blog::model::Post>();
    }
    return blog::db::RepositoryResult<blog::model::Post>::Success(post);
  }

  blog::db::RepositoryResult<bool> UpdatePost(
      const blog::model::Post&) override {
    return Failure<bool>();
  }

  blog::db::RepositoryResult<bool> DeletePost(
      const std::string&) override {
    return Failure<bool>();
  }

  blog::db::RepositoryResult<std::vector<blog::model::Post>> GetAllPosts(
      std::int64_t, std::int64_t) override {
    if (error != blog::db::RepositoryError::kNone) {
      return Failure<std::vector<blog::model::Post>>();
    }
    return blog::db::RepositoryResult<std::vector<blog::model::Post>>::Success(
        posts);
  }

  blog::db::RepositoryResult<std::vector<std::pair<std::string, int>>>
  CountPostsPerAuthor() override {
    return Failure<std::vector<std::pair<std::string, int>>>();
  }

  blog::db::RepositoryResult<bool> Ping() override { return Failure<bool>(); }

  blog::db::RepositoryError error{blog::db::RepositoryError::kNone};
  blog::model::Post post;
  std::vector<blog::model::Post> posts;
};

TEST(BlogServiceTest, MapsInvalidRepositoryInputToInvalidArgument) {
  ServiceTestStore store;
  store.error = blog::db::RepositoryError::kInvalidArgument;
  blog::api::BlogServiceImpl service(store);
  grpc::ServerContext context;
  blog::PostResponse request;
  request.set_id("invalid");
  blog::FullPostResponse response;

  const auto status = service.GetPost(&context, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(BlogServiceTest, MapsDatabaseFailureToUnavailable) {
  ServiceTestStore store;
  store.error = blog::db::RepositoryError::kUnavailable;
  blog::api::BlogServiceImpl service(store);
  grpc::ServerContext context;
  blog::PostResponse request;
  request.set_id("0123456789abcdef01234567");
  blog::FullPostResponse response;

  const auto status = service.GetPost(&context, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
}

TEST(BlogServiceTest, GetAllPostsReturnsEverySupportedField) {
  ServiceTestStore store;
  blog::model::Post post;
  post.id = "0123456789abcdef01234567";
  post.title = "title";
  post.author = "author";
  post.content = "content";
  post.tags = {"cpp", "grpc"};
  post.published_date = "2026-09-10T12:00:00Z";
  store.posts.push_back(post);

  blog::api::BlogServiceImpl service(store);
  grpc::ServerContext context;
  blog::PostResponse request;
  blog::AllPostsResponse response;

  const auto status = service.GetAllPosts(&context, &request, &response);

  ASSERT_TRUE(status.ok());
  ASSERT_EQ(response.posts_size(), 1);
  EXPECT_EQ(response.posts(0).tags_size(), 2);
  EXPECT_EQ(response.posts(0).published_date(), "2026-09-10T12:00:00Z");
}

}  // namespace
