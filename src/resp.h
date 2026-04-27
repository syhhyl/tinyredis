#pragma once

#include <string>
#include <vector>

enum class ParseResult {
  Complete,
  Incomplete,
  Error,
};

ParseResult parseRespCommand(std::string *input, std::vector<std::string> *command);
