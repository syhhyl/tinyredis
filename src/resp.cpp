#include "resp.h"

namespace {

bool isRequestTooLarge(size_t bytes) {
  return bytes > kMaxRespRequestBytes;
}

bool readLine(const std::string& input, size_t* pos, std::string* line) {
  size_t end = input.find("\r\n", *pos);
  if (end == std::string::npos) {
    return false;
  }

  *line = input.substr(*pos, end - *pos);
  *pos = end + 2;
  return true;
}

ParseResult parseLength(const std::string& s, size_t maxValue, size_t* value) {
  if (s.empty()) {
    return ParseResult::Error;
  }

  size_t result = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return ParseResult::Error;
    }
    size_t digit = static_cast<size_t>(c - '0');
    if (result > (maxValue - digit) / 10) {
      return ParseResult::TooLarge;
    }
    result = result * 10 + (c - '0');
  }

  *value = result;
  return ParseResult::Complete;
}

}  // namespace

ParseResult parseRespCommand(std::string* input, std::vector<std::string>* command) {
  size_t pos = 0;
  std::string line;

  if (!readLine(*input, &pos, &line)) {
    if (isRequestTooLarge(input->size())) {
      return ParseResult::TooLarge;
    }
    return ParseResult::Incomplete;
  }
  if (isRequestTooLarge(pos)) {
    return ParseResult::TooLarge;
  }
  if (line.empty() || line[0] != '*') {
    return ParseResult::Error;
  }

  size_t arrayLen = 0;
  ParseResult lengthResult = parseLength(line.substr(1), kMaxRespArrayLength, &arrayLen);
  if (lengthResult != ParseResult::Complete) {
    return lengthResult;
  }

  std::vector<std::string> parsed;
  parsed.reserve(arrayLen);

  for (size_t i = 0; i < arrayLen; ++i) {
    if (!readLine(*input, &pos, &line)) {
      if (isRequestTooLarge(input->size())) {
        return ParseResult::TooLarge;
      }
      return ParseResult::Incomplete;
    }
    if (isRequestTooLarge(pos)) {
      return ParseResult::TooLarge;
    }
    if (line.empty() || line[0] != '$') {
      return ParseResult::Error;
    }

    size_t bulkLen = 0;
    lengthResult = parseLength(line.substr(1), kMaxRespBulkLength, &bulkLen);
    if (lengthResult != ParseResult::Complete) {
      return lengthResult;
    }
    if (bulkLen > kMaxRespRequestBytes || isRequestTooLarge(pos + bulkLen + 2)) {
      return ParseResult::TooLarge;
    }

    if (input->size() < pos + bulkLen + 2) {
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

std::string encodeSimpleString(const std::string& value) {
  return "+" + value + "\r\n";
}

std::string encodeError(const std::string& message) {
  return "-ERR " + message + "\r\n";
}

std::string encodeInteger(int value) {
  return ":" + std::to_string(value) + "\r\n";
}

std::string encodeBulkString(const std::string& value) {
  return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

std::string encodeNullBulkString() {
  return "$-1\r\n";
}
