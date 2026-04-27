#include "resp.h"

namespace {

bool readLine(const std::string& input, size_t* pos, std::string* line) {
  size_t end = input.find("\r\n", *pos);
  if (end == std::string::npos) {
    return false;
  }

  *line = input.substr(*pos, end - *pos);
  *pos = end + 2;
  return true;
}

bool parseInt(const std::string& s, int* value) {
  if (s.empty()) {
    return false;
  }

  int result = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
    result = result * 10 + (c - '0');
  }

  *value = result;
  return true;
}

}  // namespace

ParseResult parseRespCommand(std::string* input, std::vector<std::string>* command) {
  size_t pos = 0;
  std::string line;

  if (!readLine(*input, &pos, &line)) {
    return ParseResult::Incomplete;
  }
  if (line.empty() || line[0] != '*') {
    return ParseResult::Error;
  }

  int arrayLen = 0;
  if (!parseInt(line.substr(1), &arrayLen)) {
    return ParseResult::Error;
  }

  std::vector<std::string> parsed;
  parsed.reserve(arrayLen);

  for (int i = 0; i < arrayLen; ++i) {
    if (!readLine(*input, &pos, &line)) {
      return ParseResult::Incomplete;
    }
    if (line.empty() || line[0] != '$') {
      return ParseResult::Error;
    }

    int bulkLen = 0;
    if (!parseInt(line.substr(1), &bulkLen)) {
      return ParseResult::Error;
    }

    if (input->size() < pos + static_cast<size_t>(bulkLen) + 2) {
      return ParseResult::Incomplete;
    }
    if ((*input)[pos + bulkLen] != '\r' || (*input)[pos + bulkLen + 1] != '\n') {
      return ParseResult::Error;
    }

    parsed.emplace_back(input->substr(pos, bulkLen));
    pos += bulkLen + 2;
  }

  input->erase(0, pos);
  *command = std::move(parsed);
  return ParseResult::Complete;
}
