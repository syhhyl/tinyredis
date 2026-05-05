#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

constexpr size_t kMaxRespArrayLength = 1024;
constexpr size_t kMaxRespBulkLength = 1024 * 1024;
constexpr size_t kMaxRespRequestBytes = 4 * 1024 * 1024;

enum class ParseResult {
  Complete,
  Incomplete,
  Error,
  TooLarge,
};

ParseResult parseRespCommand(std::string *input, std::vector<std::string> *command);

std::string encodeSimpleString(const std::string& value);
std::string encodeError(const std::string& message);
std::string encodeInteger(const long long value);
std::string encodeBulkString(const std::string& value);
std::string encodeNullBulkString();
std::string encodeBulkStringArray(const std::vector<std::optional<std::string>>& values);
