#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "model/blog_models.h"
#include "model/time_utils.h"

namespace blog::model {

inline constexpr std::size_t kMaxTitleLength = 200;
inline constexpr std::size_t kMaxAuthorLength = 100;
inline constexpr std::size_t kMaxContentLength = 1024 * 1024;
inline constexpr std::size_t kMaxTagLength = 64;
inline constexpr std::size_t kMaxTags = 50;
inline constexpr std::size_t kMaxPublishedDateLength = 64;
inline constexpr std::size_t kMaxComments = 100;
inline constexpr std::size_t kMaxCommentUserLength = 100;
inline constexpr std::size_t kMaxCommentContentLength = 16 * 1024;

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
  if (!post.published_date.empty() && !ParseUtcTimestamp(post.published_date).has_value()) {
    return "published_date must use UTC format YYYY-MM-DDTHH:MM:SSZ";
  }
  if (post.tags.size() > kMaxTags) {
    return "a post may contain at most 50 tags";
  }
  for (const auto& tag : post.tags) {
    if (tag.empty() || tag.size() > kMaxTagLength) {
      return "tags must contain between 1 and 64 UTF-8 bytes";
    }
  }
  if (post.comments.size() > kMaxComments) {
    return "a post may contain at most 100 embedded comments";
  }
  for (const auto& comment : post.comments) {
    if (comment.user.empty() || comment.user.size() > kMaxCommentUserLength) {
      return "comment users must contain between 1 and 100 UTF-8 bytes";
    }
    if (comment.content.empty() || comment.content.size() > kMaxCommentContentLength) {
      return "comment content must contain between 1 and 16384 UTF-8 bytes";
    }
    if (!comment.timestamp.empty() && !ParseUtcTimestamp(comment.timestamp).has_value()) {
      return "comment timestamps must use UTC format YYYY-MM-DDTHH:MM:SSZ";
    }
  }
  return std::nullopt;
}

inline std::optional<std::string> ValidatePostQuery(const PostQuery& query) {
  if (query.limit < 1 || query.limit > 100 || query.offset < 0) {
    return "limit must be between 1 and 100 and offset must be non-negative";
  }
  if (query.author && (query.author->empty() || query.author->size() > kMaxAuthorLength)) {
    return "invalid author filter";
  }
  if (query.tag && (query.tag->empty() || query.tag->size() > kMaxTagLength)) {
    return "invalid tag filter";
  }
  const auto from = query.published_from ? ParseUtcTimestamp(*query.published_from)
                                         : std::optional<std::chrono::system_clock::time_point>{};
  const auto to = query.published_to ? ParseUtcTimestamp(*query.published_to)
                                     : std::optional<std::chrono::system_clock::time_point>{};
  if ((query.published_from && !from) || (query.published_to && !to) ||
      (from && to && *from > *to)) {
    return "published_from and published_to must be ordered UTC timestamps";
  }
  return std::nullopt;
}

}  // namespace blog::model
