#pragma once

#include <chrono>
#include <cstddef>

#include "auth/auth.h"
#include "blog_service.grpc.pb.h"
#include "cache/post_cache.h"
#include "db/blog_store.h"

namespace blog::api {

class BlogServiceImpl final : public blog::BlogService::Service {
 public:
  explicit BlogServiceImpl(blog::db::BlogStore& store, std::size_t cache_capacity = 128,
                           std::chrono::milliseconds cache_ttl = std::chrono::seconds(60),
                           blog::security::Authenticator* authenticator = nullptr,
                           blog::security::FixedWindowRateLimiter* rate_limiter = nullptr,
                           bool protect_reads = false)
      : store_(store),
        cache_(cache_capacity, cache_ttl),
        authenticator_(authenticator),
        rate_limiter_(rate_limiter),
        protect_reads_(protect_reads) {}

  grpc::Status AddPost(grpc::ServerContext* context, const blog::Post* post,
                       blog::PostResponse* response) override;

  grpc::Status GetPost(grpc::ServerContext* context, const blog::PostResponse* request,
                       blog::FullPostResponse* response) override;

  grpc::Status UpdatePost(grpc::ServerContext* context, const blog::Post* post,
                          blog::PostResponse* response) override;

  grpc::Status DeletePost(grpc::ServerContext* context, const blog::PostResponse* request,
                          blog::PostResponse* response) override;

  grpc::Status GetAllPosts(grpc::ServerContext* context, const blog::PostResponse* request,
                           blog::AllPostsResponse* response) override;

  grpc::Status ListPosts(grpc::ServerContext* context, const blog::ListPostsRequest* request,
                         blog::AllPostsResponse* response) override;

 private:
  blog::db::BlogStore& store_;
  blog::cache::ThreadSafeLruPostCache cache_;
  blog::security::Authenticator* authenticator_;
  blog::security::FixedWindowRateLimiter* rate_limiter_;
  bool protect_reads_;
};

}  // namespace blog::api
