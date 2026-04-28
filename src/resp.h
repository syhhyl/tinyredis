#pragma once

#include <string>
#include <vector>

enum class ParseResult {
  Complete,
  Incomplete,
  Error,
};

ParseResult parseRespCommand(std::string *input, std::vector<std::string> *command);

std::string encodeSimpleString(const std::string& value);
std::string encodeError(const std::string& message);
std::string encodeInteger(int value);
std::string encodeBulkString(const std::string& value);
std::string encodeNullBulkString();
