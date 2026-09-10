#include "api/blog_service_impl.h"

#include <grpcpp/grpcpp.h>

#include "model/blog_models.h"
#include "model/blog_validation.h"

namespace blog::api {

namespace {

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
  model::Post model;
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);

  if (const auto error = blog::model::ValidatePost(model)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error);
  }

  auto result = store_.AddPost(model);
  if (!result.ok()) {
    return RepositoryStatus(result.error, result.message);
  }
  response->set_id(*result.value);
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::GetPost(grpc::ServerContext* context,
                                      const blog::PostResponse* request,
                                      blog::FullPostResponse* response) {
  auto result = store_.FindPostById(request->id());
  if (!result.ok()) {
    return RepositoryStatus(result.error, result.message);
  }

  CopyPost(*result.value, response->mutable_post());
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::UpdatePost(grpc::ServerContext* context,
                                         const blog::Post* post,
                                         blog::PostResponse* response) {
  if (post->id().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "post id is required");
  }

  model::Post model;
  model.id = post->id();
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);

  if (const auto error = blog::model::ValidatePost(model)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, *error);
  }

  auto result = store_.UpdatePost(model);
  if (!result.ok()) {
    return RepositoryStatus(result.error, result.message);
  }
  response->set_id(model.id);
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::DeletePost(grpc::ServerContext* context,
                                         const blog::PostResponse* request,
                                         blog::PostResponse* response) {
  auto result = store_.DeletePost(request->id());
  if (!result.ok()) {
    return RepositoryStatus(result.error, result.message);
  }
  response->set_id(request->id());
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::GetAllPosts(grpc::ServerContext* context,
                                          const blog::PostResponse* request,
                                          blog::AllPostsResponse* response) {
  auto result = store_.GetAllPosts();
  if (!result.ok()) {
    return RepositoryStatus(result.error, result.message);
  }
  for (const auto& post : *result.value) {
    CopyPost(post, response->add_posts());
  }
  return grpc::Status::OK;
}

}  // namespace blog::api
