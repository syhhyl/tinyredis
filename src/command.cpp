#include "command.h"
#include "resp.h"

#include <charconv>
#include <chrono>
#include <limits>
#include <optional>
#include <system_error>

namespace {

constexpr size_t kMaxCommandKeyLength = 1024;
constexpr size_t kMaxCommandValueLength = 1024 * 1024;

std::string toUpper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return s;
}

std::optional<long long> parsePositiveInteger(const std::string& s) {
  if (s.empty()) {
    return std::nullopt;
  }

  long long value = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc{} || ptr != s.data() + s.size() || value <= 0) {
    return std::nullopt;
  }

  return value;
}

bool isKeyTooLarge(const std::string& key) {
  return key.size() > kMaxCommandKeyLength;
}

bool isValueTooLarge(const std::string& value) {
  return value.size() > kMaxCommandValueLength;
}

}  // namespace

std::string executeCommand(const std::vector<std::string>& command, Database& db,
                           const std::string& dumpFile) {
  if (command.empty()) {
    return encodeError("empty command");
  }

  std::string name = toUpper(command[0]);
  if (name == "PING" && command.size() == 1) {
    return encodeSimpleString("PONG");
  }
  if (name == "SET") {
    if (command.size() == 3) {
      if (isKeyTooLarge(command[1]) || isValueTooLarge(command[2])) {
        return encodeError("argument too large");
      }
      db.set(command[1], command[2]);
      return encodeSimpleString("OK");
    }

    if (command.size() == 5 && toUpper(command[3]) == "EX") {
      if (isKeyTooLarge(command[1]) || isValueTooLarge(command[2])) {
        return encodeError("argument too large");
      }
      auto seconds = parsePositiveInteger(command[4]);
      if (!seconds || *seconds > std::numeric_limits<long long>::max() / 1000) {
        return encodeError("invalid expire time");
      }

      db.set(command[1], command[2], std::chrono::milliseconds(*seconds * 1000));
      return encodeSimpleString("OK");
    }
  }
  if (name == "GET" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      return encodeError("argument too large");
    }
    auto value = db.get(command[1]);
    if (!value) {
      return encodeNullBulkString();
    }
    return encodeBulkString(*value);
  }
  if (name == "EXISTS" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      return encodeError("argument too large");
    }
    return encodeInteger(db.exists(command[1]) ? 1 : 0);
  }
  if (name == "DEL" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      return encodeError("argument too large");
    }
    return encodeInteger(db.del(command[1]) ? 1 : 0);
  }
  if (name == "SAVE" && command.size() == 1) {
    if (!db.saveSnapshot(dumpFile)) {
      return encodeError("save failed");
    }
    return encodeSimpleString("OK");
  }

  return encodeError("unknown command");
}
