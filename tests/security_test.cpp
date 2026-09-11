#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "auth/auth.h"

namespace {

constexpr const char* kReader = "reader-token-with-at-least-32-characters";
constexpr const char* kWriter = "writer-token-with-at-least-32-characters";

blog::security::Authenticator MakeAuthenticator() {
  return {true,
          {{"reader", kReader, blog::security::Role::kReader},
           {"writer", kWriter, blog::security::Role::kWriter}}};
}

TEST(SecurityTest, RequiresValidBearerCredentials) {
  const auto auth = MakeAuthenticator();
  EXPECT_EQ(auth.Authenticate("", blog::security::Role::kReader).error,
            blog::security::AuthError::kMissingCredentials);
  EXPECT_EQ(auth.Authenticate("Bearer wrong", blog::security::Role::kReader).error,
            blog::security::AuthError::kInvalidCredentials);
  EXPECT_TRUE(
      auth.Authenticate(std::string("Bearer ") + kReader, blog::security::Role::kReader).ok());
}

TEST(SecurityTest, EnforcesRoleHierarchy) {
  const auto auth = MakeAuthenticator();
  EXPECT_EQ(
      auth.Authenticate(std::string("Bearer ") + kReader, blog::security::Role::kWriter).error,
      blog::security::AuthError::kForbidden);
  EXPECT_TRUE(
      auth.Authenticate(std::string("Bearer ") + kWriter, blog::security::Role::kReader).ok());
}

TEST(SecurityTest, PasswordHashesAreSaltedAndVerifiable) {
  const auto first = blog::security::HashPassword("correct horse battery staple");
  const auto second = blog::security::HashPassword("correct horse battery staple");
  EXPECT_NE(first, second);
  EXPECT_EQ(first.rfind("pbkdf2-sha256$", 0), 0U);
  EXPECT_TRUE(blog::security::VerifyPassword("correct horse battery staple", first));
  EXPECT_FALSE(blog::security::VerifyPassword("wrong", first));
  EXPECT_FALSE(blog::security::VerifyPassword("password", "malformed"));
}

TEST(SecurityTest, RateLimiterRejectsRequestsPastConfiguredLimit) {
  blog::security::FixedWindowRateLimiter limiter(2);
  EXPECT_TRUE(limiter.Allow("client"));
  EXPECT_TRUE(limiter.Allow("client"));
  EXPECT_FALSE(limiter.Allow("client"));
  EXPECT_TRUE(limiter.Allow("different-client"));
}

}  // namespace
