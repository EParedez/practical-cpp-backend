#pragma once

#include <httplib.h>

#include "auth/auth.h"
#include "db/blog_store.h"

namespace blog::cache {
class PostCache;
}

namespace blog::server {

struct HttpServerOptions {
  int read_timeout_seconds{5};
  int write_timeout_seconds{5};
  int keep_alive_timeout_seconds{10};
  int keep_alive_max_count{20};
  bool protect_read_endpoints{false};
  std::string cors_allowed_origin;
  blog::security::Authenticator* authenticator{nullptr};
  blog::security::FixedWindowRateLimiter* rate_limiter{nullptr};
};

void ConfigureHttpServer(httplib::Server& server, blog::db::BlogStore& store,
                         const HttpServerOptions& options = {},
                         blog::cache::PostCache* cache = nullptr);

}  // namespace blog::server
