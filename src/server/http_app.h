#pragma once

#include <httplib.h>

#include "db/blog_store.h"

namespace blog::server {

struct HttpServerOptions {
  int read_timeout_seconds{5};
  int write_timeout_seconds{5};
  int keep_alive_timeout_seconds{10};
  int keep_alive_max_count{20};
};

void ConfigureHttpServer(httplib::Server& server, blog::db::BlogStore& store,
                         const HttpServerOptions& options = {});

}  // namespace blog::server
