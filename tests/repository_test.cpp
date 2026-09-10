#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "db/blog_repository.h"
#include "model/blog_models.h"

using blog::db::BlogRepository;
using blog::model::Post;
using blog::model::User;

namespace {

const char* kMongoUri =
    "mongodb://localhost:27017/?serverSelectionTimeoutMS=2000&connectTimeoutMS=2000";

}  // namespace

class BlogRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo = std::make_unique<BlogRepository>(kMongoUri, "blog_test");

    // Clean the posts collection to make tests deterministic.
    try {
      auto client = repo->pool().acquire();
      auto database = (*client)["blog_test"];
      database["posts"].delete_many({});
      database["users"].delete_many({});
    } catch (const std::exception& e) {
      std::cerr << "setup cleanup failed: " << e.what() << std::endl;
    }
  }

  std::unique_ptr<BlogRepository> repo;
};

TEST_F(BlogRepositoryTest, AddAndFindPost) {
  Post post;
  post.title = "Integration Post";
  post.author = "tester";
  post.content = "Some content";
  post.published_date = "2023-07-01T12:00:00Z";

  auto add_result = repo->AddPost(post);
  ASSERT_TRUE(add_result.ok()) << add_result.message;
  const std::string id = *add_result.value;

  auto maybe = repo->FindPostById(id);
  ASSERT_TRUE(maybe.ok()) << maybe.message;
  EXPECT_EQ(maybe.value->title, "Integration Post");
  EXPECT_EQ(maybe.value->author, "tester");
}

TEST_F(BlogRepositoryTest, UpdatePost) {
  Post post;
  post.title = "Original";
  post.author = "tester";
  post.content = "before";
  auto add_result = repo->AddPost(post);
  ASSERT_TRUE(add_result.ok()) << add_result.message;
  const std::string id = *add_result.value;

  post.id = id;
  post.title = "Updated";
  post.author = "updated-author";
  post.content = "after";
  post.published_date = "2026-09-10T12:00:00Z";
  EXPECT_TRUE(repo->UpdatePost(post).ok());

  auto maybe = repo->FindPostById(id);
  ASSERT_TRUE(maybe.ok()) << maybe.message;
  EXPECT_EQ(maybe.value->title, "Updated");
  EXPECT_EQ(maybe.value->author, "updated-author");
  EXPECT_EQ(maybe.value->content, "after");
  EXPECT_EQ(maybe.value->published_date, "2026-09-10T12:00:00Z");
}

TEST_F(BlogRepositoryTest, DeletePost) {
  Post post;
  post.title = "Doomed";
  post.author = "tester";
  auto add_result = repo->AddPost(post);
  ASSERT_TRUE(add_result.ok()) << add_result.message;
  const std::string id = *add_result.value;

  EXPECT_TRUE(repo->DeletePost(id).ok());
  EXPECT_EQ(repo->FindPostById(id).error,
            blog::db::RepositoryError::kNotFound);
}

TEST_F(BlogRepositoryTest, GetAllPostsReturnsAll) {
  for (int i = 0; i < 5; ++i) {
    Post post;
    post.title = "Post " + std::to_string(i);
    post.author = "tester";
    repo->AddPost(post);
  }
  auto result = repo->GetAllPosts();
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.value->size(), 5u);
}

TEST_F(BlogRepositoryTest, CountPostsPerAuthor) {
  for (int i = 0; i < 3; ++i) {
    Post a;
    a.title = "A" + std::to_string(i);
    a.author = "alice";
    repo->AddPost(a);
  }
  for (int i = 0; i < 2; ++i) {
    Post b;
    b.title = "B" + std::to_string(i);
    b.author = "bob";
    repo->AddPost(b);
  }

  auto counts = repo->CountPostsPerAuthor();
  ASSERT_TRUE(counts.ok()) << counts.message;
  ASSERT_EQ(counts.value->size(), 2u);
  auto find = [&counts](const std::string& author) {
    for (const auto& c : *counts.value) {
      if (c.first == author) return c.second;
    }
    return -1;
  };
  EXPECT_EQ(find("alice"), 3);
  EXPECT_EQ(find("bob"), 2);
}

TEST_F(BlogRepositoryTest, CreateAndFindUser) {
  User user;
  user.username = "john_doe";
  user.email = "john@myblogapp.com";
  user.password = "hashed_password";
  user.profiles.emplace_back("twitter", "@johndoe");
  user.profiles.emplace_back("instagram", "@johndoeig");

  EXPECT_TRUE(repo->CreateUser(user).ok());

  auto maybe = repo->FindUserByUsername("john_doe");
  ASSERT_TRUE(maybe.ok()) << maybe.message;
  EXPECT_EQ(maybe.value->email, "john@myblogapp.com");

  EXPECT_TRUE(repo->UpdateUserEmail("john_doe", "new@myblogapp.com").ok());
  auto updated = repo->FindUserByUsername("john_doe");
  ASSERT_TRUE(updated.ok()) << updated.message;
  EXPECT_EQ(updated.value->email, "new@myblogapp.com");

  EXPECT_TRUE(repo->DeleteUser("john_doe").ok());
  EXPECT_EQ(repo->FindUserByUsername("john_doe").error,
            blog::db::RepositoryError::kNotFound);
}

TEST_F(BlogRepositoryTest, RejectsMalformedPostIdWithoutDatabaseLookup) {
  auto result = repo->FindPostById("not-an-object-id");
  EXPECT_EQ(result.error, blog::db::RepositoryError::kInvalidArgument);
}

TEST_F(BlogRepositoryTest, IdempotentUpdateStillSucceeds) {
  Post post;
  post.title = "No change";
  post.author = "tester";
  post.content = "same content";
  auto add_result = repo->AddPost(post);
  ASSERT_TRUE(add_result.ok()) << add_result.message;

  post.id = *add_result.value;
  EXPECT_TRUE(repo->UpdatePost(post).ok());
  EXPECT_TRUE(repo->UpdatePost(post).ok());
}
