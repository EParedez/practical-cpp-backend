#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "model/blog_models.h"

namespace blog::model {

inline constexpr std::size_t kMaxTitleLength = 200;
inline constexpr std::size_t kMaxAuthorLength = 100;
inline constexpr std::size_t kMaxContentLength = 1024 * 1024;
inline constexpr std::size_t kMaxTagLength = 64;
inline constexpr std::size_t kMaxTags = 50;
inline constexpr std::size_t kMaxPublishedDateLength = 64;

inline std::optional<std::string> ValidatePost(const Post& post) {
  if (post.title.empty() || post.author.empty()) {
    return "title and author are required";
  }
  if (post.title.size() > kMaxTitleLength) {
    return "title must contain at most 200 UTF-8 bytes";
  }
  if (post.author.size() > kMaxAuthorLength) {
    return "author must contain at most 100 UTF-8 bytes";
  }
  if (post.content.size() > kMaxContentLength) {
    return "content must contain at most 1048576 UTF-8 bytes";
  }
  if (post.published_date.size() > kMaxPublishedDateLength) {
    return "published_date must contain at most 64 UTF-8 bytes";
  }
  if (post.tags.size() > kMaxTags) {
    return "a post may contain at most 50 tags";
  }
  for (const auto& tag : post.tags) {
    if (tag.empty() || tag.size() > kMaxTagLength) {
      return "tags must contain between 1 and 64 UTF-8 bytes";
    }
  }
  return std::nullopt;
}

}  // namespace blog::model
