#include "server/http_app.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include "model/blog_validation.h"

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

namespace blog::server {
namespace {

constexpr std::size_t kMaxRequestBodyLength =
    model::kMaxContentLength + 16 * 1024;
constexpr std::size_t kMaxHeaderCount = 50;
constexpr std::size_t kMaxCombinedHeaderLength = 16 * 1024;
constexpr std::int64_t kDefaultPageSize = 20;
constexpr std::int64_t kMaximumPageSize = 100;

std::string RequestId(const httplib::Request& request) {
  if (request.has_header("X-Request-ID")) {
    const auto supplied = request.get_header_value("X-Request-ID");
    const bool safe = !supplied.empty() && supplied.size() <= 128 &&
                      std::all_of(
                          supplied.begin(), supplied.end(),
                          [](unsigned char character) {
                            return std::isalnum(character) != 0 ||
                                   character == '-' || character == '_' ||
                                   character == '.';
                          });
    if (safe) return supplied;
  }
  static std::atomic<std::uint64_t> sequence{0};
  return "request-" + std::to_string(++sequence);
}

bool HasJsonContentType(const httplib::Request& request) {
  return request.has_header("Content-Type") &&
         request.get_header_value("Content-Type").find("application/json") == 0;
}

void SetJson(httplib::Response& response, const bsoncxx::document::view& value,
             int status = 200) {
  response.status = status;
  response.set_content(
      bsoncxx::to_json(value, bsoncxx::ExtendedJsonMode::k_relaxed),
      "application/json");
}

void SetJson(httplib::Response& response, const bsoncxx::array::view& value,
             int status = 200) {
  response.status = status;
  response.set_content(
      bsoncxx::to_json(value, bsoncxx::ExtendedJsonMode::k_relaxed),
      "application/json");
}

void SetError(const httplib::Request& request, httplib::Response& response,
              int status, const std::string& code,
              const std::string& message) {
  const auto request_id = RequestId(request);
  const auto body = make_document(kvp(
      "error", make_document(kvp("code", code), kvp("message", message),
                             kvp("request_id", request_id))));
  response.set_header("X-Request-ID", request_id);
  SetJson(response, body.view(), status);
}

void SetRepositoryError(const httplib::Request& request,
                        httplib::Response& response,
                        blog::db::RepositoryError error,
                        const std::string& message) {
  using blog::db::RepositoryError;
  switch (error) {
    case RepositoryError::kInvalidArgument:
      SetError(request, response, 400, "invalid_argument", message);
      return;
    case RepositoryError::kNotFound:
      SetError(request, response, 404, "not_found", message);
      return;
    case RepositoryError::kConflict:
      SetError(request, response, 409, "conflict", message);
      return;
    case RepositoryError::kUnavailable:
      SetError(request, response, 503, "database_unavailable",
               "database unavailable");
      return;
    case RepositoryError::kInternal:
    case RepositoryError::kNone:
      SetError(request, response, 500, "internal_error", "internal error");
      return;
  }
}

bsoncxx::document::value PostDocument(const model::Post& post) {
  bsoncxx::builder::basic::array tags;
  for (const auto& tag : post.tags) tags.append(tag);
  return make_document(kvp("id", post.id), kvp("title", post.title),
                       kvp("author", post.author),
                       kvp("content", post.content),
                       kvp("tags", tags.view()),
                       kvp("published_date", post.published_date));
}

blog::db::RepositoryResult<model::Post> ParsePost(
    const httplib::Request& request) {
  if (!HasJsonContentType(request)) {
    return blog::db::RepositoryResult<model::Post>::Failure(
        blog::db::RepositoryError::kInvalidArgument,
        "Content-Type must be application/json");
  }
  if (request.body.empty()) {
    return blog::db::RepositoryResult<model::Post>::Failure(
        blog::db::RepositoryError::kInvalidArgument, "request body is required");
  }

  try {
    const auto document = bsoncxx::from_json(request.body);
    const auto view = document.view();
    model::Post post;

    const auto title = view["title"];
    const auto author = view["author"];
    if (!title || title.type() != bsoncxx::type::k_string ||
        !author || author.type() != bsoncxx::type::k_string) {
      return blog::db::RepositoryResult<model::Post>::Failure(
          blog::db::RepositoryError::kInvalidArgument,
          "title and author must be strings");
    }
    post.title = std::string(title.get_string().value);
    post.author = std::string(author.get_string().value);

    const auto content = view["content"];
    if (content) {
      if (content.type() != bsoncxx::type::k_string) {
        return blog::db::RepositoryResult<model::Post>::Failure(
            blog::db::RepositoryError::kInvalidArgument,
            "content must be a string");
      }
      post.content = std::string(content.get_string().value);
    }

    const auto published_date = view["published_date"];
    if (published_date) {
      if (published_date.type() != bsoncxx::type::k_string) {
        return blog::db::RepositoryResult<model::Post>::Failure(
            blog::db::RepositoryError::kInvalidArgument,
            "published_date must be a string");
      }
      post.published_date =
          std::string(published_date.get_string().value);
    }

    const auto tags = view["tags"];
    if (tags) {
      if (tags.type() != bsoncxx::type::k_array) {
        return blog::db::RepositoryResult<model::Post>::Failure(
            blog::db::RepositoryError::kInvalidArgument,
            "tags must be an array of strings");
      }
      for (const auto& tag : tags.get_array().value) {
        if (tag.type() != bsoncxx::type::k_string) {
          return blog::db::RepositoryResult<model::Post>::Failure(
              blog::db::RepositoryError::kInvalidArgument,
              "tags must be an array of strings");
        }
        post.tags.emplace_back(tag.get_string().value);
      }
    }

    if (const auto error = model::ValidatePost(post)) {
      return blog::db::RepositoryResult<model::Post>::Failure(
          blog::db::RepositoryError::kInvalidArgument, *error);
    }
    return blog::db::RepositoryResult<model::Post>::Success(std::move(post));
  } catch (const bsoncxx::exception&) {
    return blog::db::RepositoryResult<model::Post>::Failure(
        blog::db::RepositoryError::kInvalidArgument, "invalid JSON body");
  }
}

bool ParseIntegerParameter(const httplib::Request& request,
                           const std::string& name, std::int64_t default_value,
                           std::int64_t* output) {
  if (!request.has_param(name)) {
    *output = default_value;
    return true;
  }
  const auto raw = request.get_param_value(name);
  try {
    std::size_t parsed = 0;
    const auto value = std::stoll(raw, &parsed);
    if (parsed != raw.size()) return false;
    *output = value;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

void ConfigureHttpServer(httplib::Server& server, blog::db::BlogStore& store) {
  server.set_payload_max_length(kMaxRequestBodyLength);
  server.set_read_timeout(5, 0);
  server.set_write_timeout(5, 0);
  server.set_keep_alive_timeout(10);
  server.set_keep_alive_max_count(20);
  server.set_pre_routing_handler(
      [](const httplib::Request& request, httplib::Response& response) {
        std::size_t combined_length = 0;
        for (const auto& [name, value] : request.headers) {
          combined_length += name.size() + value.size();
        }
        if (request.headers.size() > kMaxHeaderCount ||
            combined_length > kMaxCombinedHeaderLength) {
          SetError(request, response, 431, "request_headers_too_large",
                   "request headers exceed the allowed size");
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });
  server.set_exception_handler(
      [](const httplib::Request& request, httplib::Response& response,
         std::exception_ptr) {
        SetError(request, response, 500, "internal_error", "internal error");
      });
  server.set_error_handler(
      [](const httplib::Request& request, httplib::Response& response) {
        if (!response.body.empty()) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (response.status == 404) {
          SetError(request, response, 404, "route_not_found",
                   "route not found");
        } else {
          SetError(request, response, response.status, "http_error",
                   "request failed");
        }
        return httplib::Server::HandlerResponse::Handled;
      });

  server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
    response.set_content("ok", "text/plain");
  });

  server.Get("/ready", [&store](const httplib::Request& request,
                                httplib::Response& response) {
    const auto result = store.Ping();
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }
    const auto body = make_document(kvp("status", "ready"));
    SetJson(response, body.view());
  });

  server.Get("/posts", [&store](const httplib::Request& request,
                                httplib::Response& response) {
    std::int64_t limit = 0;
    std::int64_t offset = 0;
    if (!ParseIntegerParameter(request, "limit", kDefaultPageSize, &limit) ||
        !ParseIntegerParameter(request, "offset", 0, &offset) || limit < 1 ||
        limit > kMaximumPageSize || offset < 0) {
      SetError(request, response, 400, "invalid_pagination",
               "limit must be between 1 and 100 and offset must be non-negative");
      return;
    }

    const auto result = store.GetAllPosts(limit, offset);
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }

    bsoncxx::builder::basic::array items;
    for (const auto& post : *result.value) items.append(PostDocument(post));
    const auto body = make_document(kvp("items", items.view()),
                                    kvp("limit", limit), kvp("offset", offset),
                                    kvp("count", static_cast<std::int64_t>(
                                                     result.value->size())));
    SetJson(response, body.view());
  });

  server.Post("/posts", [&store](const httplib::Request& request,
                                 httplib::Response& response) {
    if (!HasJsonContentType(request)) {
      SetError(request, response, 415, "unsupported_media_type",
               "Content-Type must be application/json");
      return;
    }
    auto parsed = ParsePost(request);
    if (!parsed.ok()) {
      SetError(request, response, 400, "invalid_post", parsed.message);
      return;
    }
    const auto result = store.AddPost(*parsed.value);
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }
    const auto body = make_document(kvp("id", *result.value));
    SetJson(response, body.view(), 201);
  });

  server.Get("/posts/:id", [&store](const httplib::Request& request,
                                    httplib::Response& response) {
    const auto result = store.FindPostById(request.path_params.at("id"));
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }
    const auto body = PostDocument(*result.value);
    SetJson(response, body.view());
  });

  server.Put("/posts/:id", [&store](const httplib::Request& request,
                                    httplib::Response& response) {
    if (!HasJsonContentType(request)) {
      SetError(request, response, 415, "unsupported_media_type",
               "Content-Type must be application/json");
      return;
    }
    auto parsed = ParsePost(request);
    if (!parsed.ok()) {
      SetError(request, response, 400, "invalid_post", parsed.message);
      return;
    }
    parsed.value->id = request.path_params.at("id");
    const auto result = store.UpdatePost(*parsed.value);
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }
    const auto body = PostDocument(*parsed.value);
    SetJson(response, body.view());
  });

  server.Delete("/posts/:id", [&store](const httplib::Request& request,
                                       httplib::Response& response) {
    const auto result = store.DeletePost(request.path_params.at("id"));
    if (!result.ok()) {
      SetRepositoryError(request, response, result.error, result.message);
      return;
    }
    const auto body = make_document(kvp("deleted", true),
                                    kvp("id", request.path_params.at("id")));
    SetJson(response, body.view());
  });

  server.Get("/stats/posts-per-author",
             [&store](const httplib::Request& request,
                      httplib::Response& response) {
               const auto result = store.CountPostsPerAuthor();
               if (!result.ok()) {
                 SetRepositoryError(request, response, result.error,
                                    result.message);
                 return;
               }
               bsoncxx::builder::basic::array counts;
               for (const auto& [author, count] : *result.value) {
                 counts.append(make_document(kvp("author", author),
                                             kvp("count", count)));
               }
               SetJson(response, counts.view());
             });
}

}  // namespace blog::server
