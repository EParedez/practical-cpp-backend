#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <unordered_set>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>

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
    const auto schema = repo->InitializeSchema();
    ASSERT_TRUE(schema.ok()) << schema.message;
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

TEST_F(BlogRepositoryTest, StoresPublicationTimestampAsBsonDate) {
  Post post;
  post.title = "Native date";
  post.author = "tester";
  post.published_date = "2026-09-10T12:00:00Z";
  const auto result = repo->AddPost(post);
  ASSERT_TRUE(result.ok()) << result.message;

  auto client = repo->pool().acquire();
  const auto stored = (*client)["blog_test"]["posts"].find_one(
      bsoncxx::builder::basic::make_document(
          bsoncxx::builder::basic::kvp(
              "_id", bsoncxx::oid{*result.value})));
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->view()["published_date"].type(), bsoncxx::type::k_date);
}

TEST_F(BlogRepositoryTest, RejectsDuplicateUsernames) {
  User first;
  first.username = "duplicate";
  first.email = "first@example.com";
  first.password = "hash";
  ASSERT_TRUE(repo->CreateUser(first).ok());
  auto second = first;
  second.email = "second@example.com";
  const auto duplicate = repo->CreateUser(second);
  EXPECT_EQ(duplicate.error, blog::db::RepositoryError::kConflict);
}

TEST_F(BlogRepositoryTest, CreatesRequiredIndexes) {
  auto client = repo->pool().acquire();
  std::unordered_set<std::string> names;
  for (const auto& index : (*client)["blog_test"]["posts"].list_indexes()) {
    names.emplace(index["name"].get_string().value);
  }
  EXPECT_EQ(names.count("posts_published_id"), 1u);
  EXPECT_EQ(names.count("posts_author_published"), 1u);
  EXPECT_EQ(names.count("posts_tags_published"), 1u);
}

TEST_F(BlogRepositoryTest, SchemaRejectsMalformedPosts) {
  auto client = repo->pool().acquire();
  EXPECT_THROW(
      (*client)["blog_test"]["posts"].insert_one(
          bsoncxx::builder::basic::make_document(
              bsoncxx::builder::basic::kvp("title", "missing fields"))),
      mongocxx::exception);
}

TEST_F(BlogRepositoryTest, FiltersAndPaginatesInDeterministicOrder) {
  for (int index = 0; index < 4; ++index) {
    Post post;
    post.title = "Post " + std::to_string(index);
    post.author = index == 3 ? "bob" : "alice";
    post.tags = index == 1 ? std::vector<std::string>{"other"}
                           : std::vector<std::string>{"cpp"};
    post.published_date = "2026-09-0" + std::to_string(index + 1) +
                          "T12:00:00Z";
    ASSERT_TRUE(repo->AddPost(post).ok());
  }
  blog::model::PostQuery query;
  query.limit = 2;
  query.author = "alice";
  query.tag = "cpp";
  query.published_from = "2026-09-01T00:00:00Z";
  query.published_to = "2026-09-04T23:59:59Z";
  const auto page = repo->GetAllPosts(query);
  ASSERT_TRUE(page.ok()) << page.message;
  ASSERT_EQ(page.value->size(), 2u);
  EXPECT_EQ(page.value->at(0).title, "Post 2");
  EXPECT_EQ(page.value->at(1).title, "Post 0");
}

TEST(BlogRepositoryMigrationTest, MigratesLegacyStringDatesOnce) {
  BlogRepository repo(kMongoUri, "blog_migration_test");
  {
    auto client = repo.pool().acquire();
    (*client)["blog_migration_test"].drop();
    (*client)["blog_migration_test"]["posts"].insert_one(
        bsoncxx::builder::basic::make_document(
            bsoncxx::builder::basic::kvp("title", "legacy"),
            bsoncxx::builder::basic::kvp("author", "tester"),
            bsoncxx::builder::basic::kvp("content", ""),
            bsoncxx::builder::basic::kvp(
                "tags", bsoncxx::builder::basic::make_array()),
            bsoncxx::builder::basic::kvp("published_date",
                                         "2026-01-02T03:04:05Z"),
            bsoncxx::builder::basic::kvp(
                "comments", bsoncxx::builder::basic::make_array(
                                bsoncxx::builder::basic::make_document(
                                    bsoncxx::builder::basic::kvp("user", "old"),
                                    bsoncxx::builder::basic::kvp("content", "text"),
                                    bsoncxx::builder::basic::kvp(
                                        "timestamp",
                                        "2026-01-02T03:04:05Z"))))));
  }
  ASSERT_TRUE(repo.InitializeSchema().ok());
  ASSERT_TRUE(repo.InitializeSchema().ok());
  auto client = repo.pool().acquire();
  const auto migrated =
      (*client)["blog_migration_test"]["posts"].find_one({});
  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(migrated->view()["published_date"].type(),
            bsoncxx::type::k_date);
  const auto comment = *migrated->view()["comments"].get_array().value.begin();
  EXPECT_EQ(comment["timestamp"].type(), bsoncxx::type::k_date);
  EXPECT_EQ((*client)["blog_migration_test"]["_schema_migrations"].count_documents(
                bsoncxx::builder::basic::make_document(
                    bsoncxx::builder::basic::kvp("_id", 1))),
            1);
}
