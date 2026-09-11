#include "auth/auth.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace blog::security {
namespace {

constexpr int kPasswordIterations = 210000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

bool ConstantTimeEqual(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) return false;
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string Hex(const unsigned char* data, std::size_t size) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < size; ++i) output << std::setw(2) << +data[i];
  return output.str();
}

std::optional<std::vector<unsigned char>> FromHex(const std::string& value) {
  if (value.size() % 2 != 0) return std::nullopt;
  std::vector<unsigned char> bytes(value.size() / 2);
  try {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      std::size_t parsed = 0;
      const auto byte = std::stoul(value.substr(i * 2, 2), &parsed, 16);
      if (parsed != 2) return std::nullopt;
      bytes[i] = static_cast<unsigned char>(byte);
    }
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return bytes;
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, delimiter)) parts.push_back(part);
  return parts;
}

}  // namespace

Authenticator::Authenticator(bool required, std::vector<TokenCredential> credentials)
    : required_(required), credentials_(std::move(credentials)) {}

AuthResult Authenticator::Authenticate(const std::string& authorization, Role required_role) const {
  if (!required_) return {Principal{"anonymous", Role::kAdmin}, AuthError::kNone};
  constexpr const char* prefix = "Bearer ";
  if (authorization.rfind(prefix, 0) != 0 ||
      authorization.size() == std::char_traits<char>::length(prefix)) {
    return {std::nullopt, AuthError::kMissingCredentials};
  }
  const auto token = authorization.substr(std::char_traits<char>::length(prefix));
  for (const auto& credential : credentials_) {
    if (!ConstantTimeEqual(token, credential.token)) continue;
    if (static_cast<int>(credential.role) < static_cast<int>(required_role)) {
      return {std::nullopt, AuthError::kForbidden};
    }
    return {Principal{credential.name, credential.role}, AuthError::kNone};
  }
  return {std::nullopt, AuthError::kInvalidCredentials};
}

FixedWindowRateLimiter::FixedWindowRateLimiter(std::size_t requests_per_minute)
    : requests_per_minute_(requests_per_minute) {}

bool FixedWindowRateLimiter::Allow(const std::string& identity) {
  if (requests_per_minute_ == 0) return true;
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  auto& window = windows_[identity];
  if (window.started.time_since_epoch().count() == 0 ||
      now - window.started >= std::chrono::minutes(1)) {
    window = {now, 1};
    return true;
  }
  if (window.count >= requests_per_minute_) return false;
  ++window.count;
  return true;
}

std::string HashPassword(const std::string& password) {
  if (password.empty()) throw std::invalid_argument("password must not be empty");
  std::array<unsigned char, kSaltBytes> salt{};
  std::array<unsigned char, kHashBytes> hash{};
  if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
      PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                        static_cast<int>(salt.size()), kPasswordIterations, EVP_sha256(),
                        static_cast<int>(hash.size()), hash.data()) != 1) {
    throw std::runtime_error("password hashing failed");
  }
  return "pbkdf2-sha256$" + std::to_string(kPasswordIterations) + "$" +
         Hex(salt.data(), salt.size()) + "$" + Hex(hash.data(), hash.size());
}

bool VerifyPassword(const std::string& password, const std::string& encoded) {
  const auto parts = Split(encoded, '$');
  if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") return false;
  int iterations = 0;
  try {
    std::size_t parsed = 0;
    iterations = std::stoi(parts[1], &parsed);
    if (parsed != parts[1].size() || iterations < 100000) return false;
  } catch (const std::exception&) {
    return false;
  }
  const auto salt = FromHex(parts[2]);
  const auto expected = FromHex(parts[3]);
  if (!salt || !expected || salt->size() < 16 || expected->empty()) return false;
  std::vector<unsigned char> actual(expected->size());
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt->data(),
                        static_cast<int>(salt->size()), iterations, EVP_sha256(),
                        static_cast<int>(actual.size()), actual.data()) != 1) {
    return false;
  }
  return CRYPTO_memcmp(actual.data(), expected->data(), actual.size()) == 0;
}

}  // namespace blog::security
