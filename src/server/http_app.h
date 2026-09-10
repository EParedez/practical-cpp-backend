#pragma once

#include <httplib.h>

#include "db/blog_store.h"

namespace blog::server {

void ConfigureHttpServer(httplib::Server& server, blog::db::BlogStore& store);

}  // namespace blog::server
