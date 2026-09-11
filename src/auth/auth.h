#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blog::security {

enum class Role { kNone = 0, kReader = 1, kWriter = 2, kAdmin = 3 };
enum class AuthError { kNone, kMissingCredentials, kInvalidCredentials, kForbidden };

struct Principal {
  std::string name;
  Role role{Role::kNone};
};

struct AuthResult {
  std::optional<Principal> principal;
  AuthError error{AuthError::kNone};

  [[nodiscard]] bool ok() const { return error == AuthError::kNone; }
};

struct TokenCredential {
  std::string name;
  std::string token;
  Role role{Role::kNone};
};

class Authenticator {
 public:
  Authenticator(bool required, std::vector<TokenCredential> credentials);

  [[nodiscard]] AuthResult Authenticate(const std::string& authorization, Role required_role) const;
  [[nodiscard]] bool required() const { return required_; }

 private:
  bool required_;
  std::vector<TokenCredential> credentials_;
};

class FixedWindowRateLimiter {
 public:
  explicit FixedWindowRateLimiter(std::size_t requests_per_minute);
  [[nodiscard]] bool Allow(const std::string& identity);

 private:
  struct Window {
    std::chrono::steady_clock::time_point started;
    std::size_t count{0};
  };

  std::size_t requests_per_minute_;
  std::mutex mutex_;
  std::unordered_map<std::string, Window> windows_;
};

std::string HashPassword(const std::string& password);
bool VerifyPassword(const std::string& password, const std::string& encoded);

}  // namespace blog::security
