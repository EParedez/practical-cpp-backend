#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace blog::model {

std::optional<std::chrono::system_clock::time_point> ParseUtcTimestamp(
    const std::string& value);
std::string FormatUtcTimestamp(
    std::chrono::system_clock::time_point value);
std::string CurrentUtcTimestamp();

}  // namespace blog::model

