#include "model/time_utils.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace blog::model {

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point value) {
  const auto raw = std::chrono::system_clock::to_time_t(value);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &raw);
#else
  gmtime_r(&raw, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

std::optional<std::chrono::system_clock::time_point> ParseUtcTimestamp(const std::string& value) {
  if (value.size() != 20 || value.back() != 'Z') return std::nullopt;
  std::tm utc{};
  std::istringstream input(value);
  input >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  if (input.fail() || input.peek() != std::char_traits<char>::eof()) {
    return std::nullopt;
  }
#if defined(_WIN32)
  const auto raw = _mkgmtime(&utc);
#else
  const auto raw = timegm(&utc);
#endif
  if (raw == static_cast<std::time_t>(-1)) return std::nullopt;
  const auto result = std::chrono::system_clock::from_time_t(raw);
  if (FormatUtcTimestamp(result) != value) return std::nullopt;
  return result;
}

std::string CurrentUtcTimestamp() { return FormatUtcTimestamp(std::chrono::system_clock::now()); }

}  // namespace blog::model
