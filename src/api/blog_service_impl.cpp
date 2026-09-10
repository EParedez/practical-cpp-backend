#include "api/blog_service_impl.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

#include "common/observability.h"
#include "model/blog_models.h"
#include "model/blog_validation.h"

namespace blog::api {

namespace {

class RpcCall {
 public:
  RpcCall(grpc::ServerContext* context, std::string operation)
      : operation_(std::move(operation)) {
    const auto metadata = context->client_metadata().find("x-request-id");
    if (metadata != context->client_metadata().end() &&
        !metadata->second.empty() && metadata->second.size() <= 128) {
      request_id_.assign(metadata->second.data(), metadata->second.size());
    } else {
      static std::atomic<std::uint64_t> sequence{0};
      request_id_ = "grpc-" + std::to_string(++sequence);
    }
    context->AddInitialMetadata("x-request-id", request_id_);
  }

  grpc::Status Finish(grpc::Status status) {
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started_)
                                 .count();
    const bool failed = !status.ok();
    observability::Metrics::Instance().RecordGrpcRequest(failed);
    observability::Log(
        status.error_code() == grpc::StatusCode::INTERNAL ||
                status.error_code() == grpc::StatusCode::UNAVAILABLE
            ? "error"
            : "info",
        "grpc_request",
        {{"request_id", request_id_},
         {"operation", operation_},
         {"status", std::to_string(status.error_code())},
         {"duration_ms", std::to_string(duration_ms)}});
    return status;
  }

 private:
  std::string operation_;
  std::string request_id_;
  std::chrono::steady_clock::time_point started_{
      std::chrono::steady_clock::now()};
};

grpc::Status RepositoryStatus(const blog::db::RepositoryError error,
                              const std::string& message) {
  using blog::db::RepositoryError;
  switch (error) {
    case RepositoryError::kInvalidArgument:
      return {grpc::StatusCode::INVALID_ARGUMENT, message};
    case RepositoryError::kNotFound:
      return {grpc::StatusCode::NOT_FOUND, message};
    case RepositoryError::kConflict:
      return {grpc::StatusCode::ALREADY_EXISTS, message};
    case RepositoryError::kUnavailable:
      return {grpc::StatusCode::UNAVAILABLE, "database unavailable"};
    case RepositoryError::kInternal:
      return {grpc::StatusCode::INTERNAL, "internal database error"};
    case RepositoryError::kNone:
      break;
  }
  return {grpc::StatusCode::INTERNAL, "unexpected repository result"};
}

void CopyPost(const model::Post& source, blog::Post* destination) {
  destination->set_id(source.id);
  destination->set_title(source.title);
  destination->set_author(source.author);
  destination->set_content(source.content);
  destination->set_published_date(source.published_date);
  for (const auto& tag : source.tags) destination->add_tags(tag);
}

}  // namespace

grpc::Status BlogServiceImpl::AddPost(grpc::ServerContext* context,
                                      const blog::Post* post,
                                      blog::PostResponse* response) {
  RpcCall call(context, "AddPost");
  model::Post model;
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);

  if (const auto error = blog::model::ValidatePost(model)) {
    return call.Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error));
  }

  auto result = store_.AddPost(model);
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  response->set_id(*result.value);
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::GetPost(grpc::ServerContext* context,
                                      const blog::PostResponse* request,
                                      blog::FullPostResponse* response) {
  RpcCall call(context, "GetPost");
  auto result = store_.FindPostById(request->id());
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }

  CopyPost(*result.value, response->mutable_post());
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::UpdatePost(grpc::ServerContext* context,
                                         const blog::Post* post,
                                         blog::PostResponse* response) {
  RpcCall call(context, "UpdatePost");
  if (post->id().empty()) {
    return call.Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                    "post id is required"));
  }

  model::Post model;
  model.id = post->id();
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);

  if (const auto error = blog::model::ValidatePost(model)) {
    return call.Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error));
  }

  auto result = store_.UpdatePost(model);
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  response->set_id(model.id);
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::DeletePost(grpc::ServerContext* context,
                                         const blog::PostResponse* request,
                                         blog::PostResponse* response) {
  RpcCall call(context, "DeletePost");
  auto result = store_.DeletePost(request->id());
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  response->set_id(request->id());
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::GetAllPosts(grpc::ServerContext* context,
                                          const blog::PostResponse* request,
                                          blog::AllPostsResponse* response) {
  RpcCall call(context, "GetAllPosts");
  auto result = store_.GetAllPosts();
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  for (const auto& post : *result.value) {
    CopyPost(post, response->add_posts());
  }
  return call.Finish(grpc::Status::OK);
}

}  // namespace blog::api
