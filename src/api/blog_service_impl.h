#pragma once

#include "blog_service.grpc.pb.h"
#include "cache/lru_cache.h"
#include "db/blog_store.h"

namespace blog::api {

class BlogServiceImpl final : public blog::BlogService::Service {
 public:
  explicit BlogServiceImpl(blog::db::BlogStore& store, int cache_capacity = 128)
      : store_(store), cache_(cache_capacity) {}

  grpc::Status AddPost(grpc::ServerContext* context,
                       const blog::Post* post,
                       blog::PostResponse* response) override;

  grpc::Status GetPost(grpc::ServerContext* context,
                       const blog::PostResponse* request,
                       blog::FullPostResponse* response) override;

  grpc::Status UpdatePost(grpc::ServerContext* context,
                          const blog::Post* post,
                          blog::PostResponse* response) override;

  grpc::Status DeletePost(grpc::ServerContext* context,
                          const blog::PostResponse* request,
                          blog::PostResponse* response) override;

  grpc::Status GetAllPosts(grpc::ServerContext* context,
                           const blog::PostResponse* request,
                           blog::AllPostsResponse* response) override;

 private:
  blog::db::BlogStore& store_;
  blog::cache::LRUCache cache_;
};

}  // namespace blog::api
