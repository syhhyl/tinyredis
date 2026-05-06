#pragma once

#include <cstddef>
#include <string>
#include <string_view>
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

void appendSimpleString(std::string& output, std::string_view value);
void appendError(std::string& output, std::string_view message);
void appendInteger(std::string& output, long long value);
void appendBulkString(std::string& output, std::string_view value);
void appendNullBulkString(std::string& output);
