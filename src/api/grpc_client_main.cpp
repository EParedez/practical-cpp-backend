#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <string>

#include "blog_service.grpc.pb.h"

int main(int argc, char** argv) {
  const std::string target = argc > 1 ? argv[1] : std::string("localhost:50051");

  auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto stub = blog::BlogService::NewStub(channel);

  // --- AddPost ---
  {
    blog::Post post;
    post.set_title("My first blog post");
    post.set_author("john_doe");
    post.set_content("Hello from gRPC client.");
    post.set_published_date("2023-07-01T12:00:00Z");
    post.add_tags("intro");
    post.add_tags("grpc");

    grpc::ClientContext ctx;
    blog::PostResponse response;
    grpc::Status status = stub->AddPost(&ctx, post, &response);
    if (!status.ok()) {
      std::cerr << "AddPost failed: " << status.error_message() << std::endl;
      return 1;
    }
    std::cout << "Created post with id=" << response.id() << std::endl;

    // --- GetPost ---
    grpc::ClientContext ctx2;
    blog::PostResponse req;
    req.set_id(response.id());
    blog::FullPostResponse full;
    status = stub->GetPost(&ctx2, req, &full);
    if (!status.ok()) {
      std::cerr << "GetPost failed: " << status.error_message() << std::endl;
      return 1;
    }
    std::cout << "Got post: [" << full.post().title() << "] by " << full.post().author()
              << std::endl;

    // --- GetAllPosts ---
    grpc::ClientContext ctx3;
    blog::AllPostsResponse all;
    status = stub->GetAllPosts(&ctx3, req, &all);
    if (!status.ok()) {
      std::cerr << "GetAllPosts failed: " << status.error_message() << std::endl;
      return 1;
    }
    std::cout << "Total posts: " << all.posts_size() << std::endl;

    // --- UpdatePost ---
    grpc::ClientContext ctx4;
    blog::Post updated = full.post();
    updated.set_content("Updated content via gRPC.");
    blog::PostResponse upd_resp;
    status = stub->UpdatePost(&ctx4, updated, &upd_resp);
    if (!status.ok()) {
      std::cerr << "UpdatePost failed: " << status.error_message() << std::endl;
      return 1;
    }
    std::cout << "Updated post id=" << upd_resp.id() << std::endl;

    // --- DeletePost ---
    grpc::ClientContext ctx5;
    blog::PostResponse del_req;
    del_req.set_id(response.id());
    blog::PostResponse del_resp;
    status = stub->DeletePost(&ctx5, del_req, &del_resp);
    if (!status.ok()) {
      std::cerr << "DeletePost failed: " << status.error_message() << std::endl;
      return 1;
    }
    std::cout << "Deleted post id=" << del_resp.id() << std::endl;
  }

  std::cout << "All RPC calls succeeded." << std::endl;
  return 0;
}
