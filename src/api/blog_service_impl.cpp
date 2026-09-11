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
  RpcCall(grpc::ServerContext* context, std::string operation) : operation_(std::move(operation)) {
    const auto metadata = context->client_metadata().find("x-request-id");
    if (metadata != context->client_metadata().end() && !metadata->second.empty() &&
        metadata->second.size() <= 128) {
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
    observability::Log(status.error_code() == grpc::StatusCode::INTERNAL ||
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
  std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
};

grpc::Status RepositoryStatus(const blog::db::RepositoryError error, const std::string& message) {
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

grpc::Status Authorize(grpc::ServerContext* context, security::Authenticator* authenticator,
                       security::FixedWindowRateLimiter* rate_limiter, security::Role required_role,
                       bool enforce) {
  if (!enforce || authenticator == nullptr) return grpc::Status::OK;
  std::string authorization;
  const auto metadata = context->client_metadata().find("authorization");
  if (metadata != context->client_metadata().end()) {
    authorization.assign(metadata->second.data(), metadata->second.size());
  }
  const auto auth = authenticator->Authenticate(authorization, required_role);
  if (!auth.ok()) {
    if (auth.error == security::AuthError::kForbidden) {
      return {grpc::StatusCode::PERMISSION_DENIED,
              "the authenticated role cannot perform this operation"};
    }
    return {grpc::StatusCode::UNAUTHENTICATED, "a valid Bearer token is required"};
  }
  if (rate_limiter != nullptr && !rate_limiter->Allow(auth.principal->name)) {
    return {grpc::StatusCode::RESOURCE_EXHAUSTED, "the request rate limit was exceeded"};
  }
  return grpc::Status::OK;
}

}  // namespace

grpc::Status BlogServiceImpl::AddPost(grpc::ServerContext* context, const blog::Post* post,
                                      blog::PostResponse* response) {
  RpcCall call(context, "AddPost");
  if (auto status =
          Authorize(context, authenticator_, rate_limiter_, security::Role::kWriter, true);
      !status.ok())
    return call.Finish(status);
  model::Post model;
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);
  if (model.published_date.empty()) {
    model.published_date = model::CurrentUtcTimestamp();
  }

  if (const auto error = blog::model::ValidatePost(model)) {
    return call.Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error));
  }

  auto result = store_.AddPost(model);
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  model.id = *result.value;
  cache_.Put(model.id, model);
  response->set_id(*result.value);
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::GetPost(grpc::ServerContext* context,
                                      const blog::PostResponse* request,
                                      blog::FullPostResponse* response) {
  RpcCall call(context, "GetPost");
  if (auto status = Authorize(context, authenticator_, rate_limiter_, security::Role::kReader,
                              protect_reads_);
      !status.ok())
    return call.Finish(status);
  if (const auto cached = cache_.Get(request->id())) {
    CopyPost(*cached, response->mutable_post());
    return call.Finish(grpc::Status::OK);
  }
  auto result = store_.FindPostById(request->id());
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }

  cache_.Put(request->id(), *result.value);
  CopyPost(*result.value, response->mutable_post());
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::UpdatePost(grpc::ServerContext* context, const blog::Post* post,
                                         blog::PostResponse* response) {
  RpcCall call(context, "UpdatePost");
  if (auto status =
          Authorize(context, authenticator_, rate_limiter_, security::Role::kWriter, true);
      !status.ok())
    return call.Finish(status);
  if (post->id().empty()) {
    return call.Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "post id is required"));
  }

  model::Post model;
  model.id = post->id();
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);
  if (model.published_date.empty()) {
    model.published_date = model::CurrentUtcTimestamp();
  }

  if (const auto error = blog::model::ValidatePost(model)) {
    return call.Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error));
  }

  auto result = store_.UpdatePost(model);
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  cache_.Put(model.id, model);
  response->set_id(model.id);
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::DeletePost(grpc::ServerContext* context,
                                         const blog::PostResponse* request,
                                         blog::PostResponse* response) {
  RpcCall call(context, "DeletePost");
  if (auto status =
          Authorize(context, authenticator_, rate_limiter_, security::Role::kWriter, true);
      !status.ok())
    return call.Finish(status);
  auto result = store_.DeletePost(request->id());
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  cache_.Invalidate(request->id());
  response->set_id(request->id());
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::GetAllPosts(grpc::ServerContext* context, const blog::PostResponse*,
                                          blog::AllPostsResponse* response) {
  RpcCall call(context, "GetAllPosts");
  if (auto status = Authorize(context, authenticator_, rate_limiter_, security::Role::kReader,
                              protect_reads_);
      !status.ok())
    return call.Finish(status);
  auto result = store_.GetAllPosts();
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  for (const auto& post : *result.value) {
    CopyPost(post, response->add_posts());
  }
  return call.Finish(grpc::Status::OK);
}

grpc::Status BlogServiceImpl::ListPosts(grpc::ServerContext* context,
                                        const blog::ListPostsRequest* request,
                                        blog::AllPostsResponse* response) {
  RpcCall call(context, "ListPosts");
  if (auto status = Authorize(context, authenticator_, rate_limiter_, security::Role::kReader,
                              protect_reads_);
      !status.ok())
    return call.Finish(status);
  model::PostQuery query;
  query.limit = request->limit() == 0 ? 20 : request->limit();
  query.offset = request->offset();
  if (!request->author().empty()) query.author = request->author();
  if (!request->tag().empty()) query.tag = request->tag();
  if (!request->published_from().empty()) {
    query.published_from = request->published_from();
  }
  if (!request->published_to().empty()) {
    query.published_to = request->published_to();
  }
  auto result = store_.GetAllPosts(query);
  if (!result.ok()) {
    return call.Finish(RepositoryStatus(result.error, result.message));
  }
  for (const auto& post : *result.value) {
    CopyPost(post, response->add_posts());
  }
  return call.Finish(grpc::Status::OK);
}

}  // namespace blog::api
