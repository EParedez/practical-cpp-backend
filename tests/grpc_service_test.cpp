#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include <atomic>
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
    ++find_post_calls;
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
      const blog::model::PostQuery& query) override {
    last_query = query;
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
  blog::model::PostQuery last_query;
  std::atomic<int> find_post_calls{0};
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

TEST(BlogServiceTest, ReusesCachedPostForRepeatedReads) {
  ServiceTestStore store;
  store.post.id = "0123456789abcdef01234567";
  store.post.title = "cached";
  store.post.author = "author";
  blog::api::BlogServiceImpl service(store);
  blog::PostResponse request;
  request.set_id(store.post.id);

  grpc::ServerContext first_context;
  blog::FullPostResponse first_response;
  ASSERT_TRUE(service.GetPost(&first_context, &request, &first_response).ok());
  grpc::ServerContext second_context;
  blog::FullPostResponse second_response;
  ASSERT_TRUE(
      service.GetPost(&second_context, &request, &second_response).ok());

  EXPECT_EQ(store.find_post_calls.load(), 1);
  EXPECT_EQ(second_response.post().title(), "cached");
}

TEST(BlogServiceTest, MapsListPostsFiltersToRepositoryQuery) {
  ServiceTestStore store;
  blog::api::BlogServiceImpl service(store);
  grpc::ServerContext context;
  blog::ListPostsRequest request;
  request.set_limit(7);
  request.set_offset(3);
  request.set_author("alice");
  request.set_tag("cpp");
  request.set_published_from("2026-01-01T00:00:00Z");
  request.set_published_to("2026-12-31T23:59:59Z");
  blog::AllPostsResponse response;

  ASSERT_TRUE(service.ListPosts(&context, &request, &response).ok());
  EXPECT_EQ(store.last_query.limit, 7);
  EXPECT_EQ(store.last_query.offset, 3);
  ASSERT_TRUE(store.last_query.author.has_value());
  EXPECT_EQ(*store.last_query.author, "alice");
  ASSERT_TRUE(store.last_query.tag.has_value());
  EXPECT_EQ(*store.last_query.tag, "cpp");
}

}  // namespace
