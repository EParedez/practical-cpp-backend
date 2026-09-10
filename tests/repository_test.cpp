#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "db/blog_repository.h"
#include "model/blog_models.h"

using blog::db::BlogRepository;
using blog::model::Post;
using blog::model::User;

namespace {

const char* kMongoUri = "mongodb://localhost:27017";

}  // namespace

class BlogRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo = std::make_unique<BlogRepository>(kMongoUri, "blog_test");

    // Clean the posts collection to make tests deterministic.
    try {
      auto client = repo->pool().acquire();
      auto collection = (*client)["blog_test"]["posts"];
      collection.delete_many({});
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

  std::string id = repo->AddPost(post);
  ASSERT_FALSE(id.empty());

  auto maybe = repo->FindPostById(id);
  ASSERT_TRUE(maybe.has_value());
  EXPECT_EQ(maybe->title, "Integration Post");
  EXPECT_EQ(maybe->author, "tester");
}

TEST_F(BlogRepositoryTest, UpdatePost) {
  Post post;
  post.title = "Original";
  post.author = "tester";
  post.content = "before";
  std::string id = repo->AddPost(post);
  ASSERT_FALSE(id.empty());

  post.id = id;
  post.title = "Updated";
  post.content = "after";
  EXPECT_TRUE(repo->UpdatePost(post));

  auto maybe = repo->FindPostById(id);
  ASSERT_TRUE(maybe.has_value());
  EXPECT_EQ(maybe->title, "Updated");
  EXPECT_EQ(maybe->content, "after");
}

TEST_F(BlogRepositoryTest, DeletePost) {
  Post post;
  post.title = "Doomed";
  post.author = "tester";
  std::string id = repo->AddPost(post);
  ASSERT_FALSE(id.empty());

  EXPECT_TRUE(repo->DeletePost(id));
  EXPECT_FALSE(repo->FindPostById(id).has_value());
}

TEST_F(BlogRepositoryTest, GetAllPostsReturnsAll) {
  for (int i = 0; i < 5; ++i) {
    Post post;
    post.title = "Post " + std::to_string(i);
    post.author = "tester";
    repo->AddPost(post);
  }
  EXPECT_EQ(repo->GetAllPosts().size(), 5u);
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
  ASSERT_EQ(counts.size(), 2u);
  auto find = [&counts](const std::string& author) {
    for (const auto& c : counts) {
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

  EXPECT_TRUE(repo->CreateUser(user));

  auto maybe = repo->FindUserByUsername("john_doe");
  ASSERT_TRUE(maybe.has_value());
  EXPECT_EQ(maybe->email, "john@myblogapp.com");

  EXPECT_TRUE(repo->UpdateUserEmail("john_doe", "new@myblogapp.com"));
  auto updated = repo->FindUserByUsername("john_doe");
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->email, "new@myblogapp.com");

  EXPECT_TRUE(repo->DeleteUser("john_doe"));
  EXPECT_FALSE(repo->FindUserByUsername("john_doe").has_value());
}
