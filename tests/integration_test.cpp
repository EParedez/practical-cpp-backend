#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "api/blog_service_impl.h"
#include "blog_service.grpc.pb.h"
#include "db/blog_repository.h"

namespace {

const char* kServerAddress = "127.0.0.1:50052";
const char* kMongoUri =
    "mongodb://localhost:27017/?serverSelectionTimeoutMS=2000&connectTimeoutMS=2000";

void SetDeadline(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() +
                        std::chrono::seconds(5));
}

class GrpcIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo = std::make_unique<blog::db::BlogRepository>(kMongoUri, "blog_itest");
    service = std::make_unique<blog::api::BlogServiceImpl>(*repo);

    auto client = repo->pool().acquire();
    (*client)["blog_itest"]["posts"].delete_many({});

    grpc::ServerBuilder builder;
    builder.AddListeningPort(kServerAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(service.get());
    server = builder.BuildAndStart();
    ASSERT_TRUE(server != nullptr);

    auto channel = grpc::CreateChannel(kServerAddress,
                                       grpc::InsecureChannelCredentials());
    stub = blog::BlogService::NewStub(channel);
  }

  void TearDown() override {
    if (server) server->Shutdown();
  }

  std::unique_ptr<blog::db::BlogRepository> repo;
  std::unique_ptr<blog::api::BlogServiceImpl> service;
  std::unique_ptr<grpc::Server> server;
  std::unique_ptr<blog::BlogService::Stub> stub;
};

}  // namespace

TEST_F(GrpcIntegrationTest, AddPostPersistsToMongoDB) {
  blog::Post post;
  post.set_title("Test Title");
  post.set_content("Test Content");
  post.set_author("tester");

  grpc::ClientContext ctx;
  SetDeadline(&ctx);
  blog::PostResponse response;
  grpc::Status status = stub->AddPost(&ctx, post, &response);

  ASSERT_TRUE(status.ok());
  ASSERT_FALSE(response.id().empty());

    auto maybe = repo->FindPostById(response.id());
    ASSERT_TRUE(maybe.ok()) << maybe.message;
    EXPECT_EQ(maybe.value->title, "Test Title");
    EXPECT_EQ(maybe.value->author, "tester");
}

TEST_F(GrpcIntegrationTest, GetPostReturnsFullPost) {
  blog::Post post;
  post.set_title("Fetch Me");
  post.set_content("Content");
  post.set_author("tester");

  grpc::ClientContext ctx_add;
  SetDeadline(&ctx_add);
  blog::PostResponse add_response;
  ASSERT_TRUE(stub->AddPost(&ctx_add, post, &add_response).ok());

  grpc::ClientContext ctx_get;
  SetDeadline(&ctx_get);
  blog::PostResponse request;
  request.set_id(add_response.id());
  blog::FullPostResponse full_response;
  grpc::Status status = stub->GetPost(&ctx_get, request, &full_response);

  ASSERT_TRUE(status.ok());
  EXPECT_EQ(full_response.post().title(), "Fetch Me");
  EXPECT_EQ(full_response.post().author(), "tester");
}

TEST_F(GrpcIntegrationTest, GetMissingPostReturnsNotFound) {
  grpc::ClientContext ctx;
  SetDeadline(&ctx);
  blog::PostResponse request;
  request.set_id("000000000000000000000000");  // non-existent ObjectId
  blog::FullPostResponse response;

  grpc::Status status = stub->GetPost(&ctx, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(GrpcIntegrationTest, AddPostRejectsEmptyAuthor) {
  blog::Post post;
  post.set_title("No Author");
  post.set_content("Content");

  grpc::ClientContext ctx;
  SetDeadline(&ctx);
  blog::PostResponse response;
  grpc::Status status = stub->AddPost(&ctx, post, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(GrpcIntegrationTest, DeletePostRemovesFromDatabase) {
  blog::Post post;
  post.set_title("Doomed");
  post.set_author("tester");
  post.set_content("content");

  grpc::ClientContext ctx_add;
  SetDeadline(&ctx_add);
  blog::PostResponse add_response;
  ASSERT_TRUE(stub->AddPost(&ctx_add, post, &add_response).ok());

  grpc::ClientContext ctx_del;
  SetDeadline(&ctx_del);
  blog::PostResponse request;
  request.set_id(add_response.id());
  blog::PostResponse del_response;
  ASSERT_TRUE(stub->DeletePost(&ctx_del, request, &del_response).ok());

  EXPECT_EQ(repo->FindPostById(add_response.id()).error,
            blog::db::RepositoryError::kNotFound);
}

TEST_F(GrpcIntegrationTest, GetAllPostsReturnsAll) {
  for (int i = 0; i < 3; ++i) {
    blog::Post post;
    post.set_title("Post " + std::to_string(i));
    post.set_author("tester");
    post.set_content("content");

    grpc::ClientContext ctx;
    SetDeadline(&ctx);
    blog::PostResponse response;
    ASSERT_TRUE(stub->AddPost(&ctx, post, &response).ok());
  }

  grpc::ClientContext ctx;
  SetDeadline(&ctx);
  blog::PostResponse request;
  blog::AllPostsResponse response;
  ASSERT_TRUE(stub->GetAllPosts(&ctx, request, &response).ok());
  EXPECT_EQ(response.posts_size(), 3);
}
