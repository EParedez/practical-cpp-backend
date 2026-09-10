#include "api/blog_service_impl.h"

#include <grpcpp/grpcpp.h>

#include "model/blog_models.h"

namespace blog::api {

grpc::Status BlogServiceImpl::AddPost(grpc::ServerContext* context,
                                      const blog::Post* post,
                                      blog::PostResponse* response) {
  if (post->title().empty() || post->author().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "title and author are required");
  }

  model::Post model;
  model.title = post->title();
  model.author = post->author();
  model.content = post->content();
  model.published_date = post->published_date();
  for (const auto& tag : post->tags()) model.tags.push_back(tag);

  std::string id = repo_.AddPost(model);
  if (id.empty()) {
    return grpc::Status(grpc::StatusCode::INTERNAL, "failed to insert post");
  }
  response->set_id(id);
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::GetPost(grpc::ServerContext* context,
                                      const blog::PostResponse* request,
                                      blog::FullPostResponse* response) {
  auto maybe = repo_.FindPostById(request->id());
  if (!maybe) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "post not found");
  }

  blog::Post* post = response->mutable_post();
  post->set_id(maybe->id);
  post->set_title(maybe->title);
  post->set_author(maybe->author);
  post->set_content(maybe->content);
  post->set_published_date(maybe->published_date);
  for (const auto& tag : maybe->tags) post->add_tags(tag);
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

  if (!repo_.UpdatePost(model)) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "post not found");
  }
  response->set_id(model.id);
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::DeletePost(grpc::ServerContext* context,
                                         const blog::PostResponse* request,
                                         blog::PostResponse* response) {
  if (!repo_.DeletePost(request->id())) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "post not found");
  }
  response->set_id(request->id());
  return grpc::Status::OK;
}

grpc::Status BlogServiceImpl::GetAllPosts(grpc::ServerContext* context,
                                          const blog::PostResponse* request,
                                          blog::AllPostsResponse* response) {
  auto posts = repo_.GetAllPosts();
  for (const auto& p : posts) {
    blog::Post* post = response->add_posts();
    post->set_id(p.id);
    post->set_title(p.title);
    post->set_author(p.author);
    post->set_content(p.content);
  }
  return grpc::Status::OK;
}

}  // namespace blog::api
