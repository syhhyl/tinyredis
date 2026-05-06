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

void appendExecuteCommand(const std::vector<std::string>& command, Database& db,
                          const std::string& dumpFile, std::string& output) {
  if (command.empty()) {
    appendError(output, "empty command");
    return;
  }

  std::string name = toUpper(command[0]);
  if (name == "PING" && command.size() == 1) {
    appendSimpleString(output, "PONG");
    return;
  }
  if (name == "SET") {
    if (command.size() == 3) {
      if (isKeyTooLarge(command[1]) || isValueTooLarge(command[2])) {
        appendError(output, "argument too large");
        return;
      }
      db.set(command[1], command[2]);
      appendSimpleString(output, "OK");
      return;
    }

    if (command.size() == 5 && toUpper(command[3]) == "EX") {
      if (isKeyTooLarge(command[1]) || isValueTooLarge(command[2])) {
        appendError(output, "argument too large");
        return;
      }
      auto seconds = parsePositiveInteger(command[4]);
      if (!seconds || *seconds > std::numeric_limits<long long>::max() / 1000) {
        appendError(output, "invalid expire time");
        return;
      }

      db.set(command[1], command[2], std::chrono::milliseconds(*seconds * 1000));
      appendSimpleString(output, "OK");
      return;
    }
  }
  if (name == "GET" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    auto value = db.get(command[1]);
    if (!value) {
      appendNullBulkString(output);
      return;
    }
    appendBulkString(output, *value);
    return;
  }
  if (name == "EXISTS" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    appendInteger(output, db.exists(command[1]) ? 1 : 0);
    return;
  }
  if (name == "DEL" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    appendInteger(output, db.del(command[1]) ? 1 : 0);
    return;
  }
  if (name == "INCR" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    auto value = db.incr(command[1]);
    if (!value) {
      appendError(output, "value is not an integer or out of range");
      return;
    }
    appendInteger(output, *value);
    return;
  }
  if (name == "DECR" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    auto value = db.decr(command[1]);
    if (!value) {
      appendError(output, "value is not an integer or out of range");
      return;
    }
    appendInteger(output, *value);
    return;
  }
  if (name == "MGET" && command.size() >= 2) {
    for (auto it = command.begin() + 1; it != command.end(); it++) {
      if (isKeyTooLarge(*it)) {
        appendError(output, "argument too large");
        return;
      }
    }
    output.push_back('*');
    output.append(std::to_string(command.size() - 1));
    output.append("\r\n");
    for (auto it = command.begin() + 1; it != command.end(); ++it) {
      auto value = db.get(*it);
      if (value) {
        appendBulkString(output, *value);
      } else {
        appendNullBulkString(output);
      }
    }
    return;
  }
  if (name == "MSET" && command.size() >= 3 && command.size() % 2 == 1) {
    for (auto it = command.begin() + 1; it != command.end(); it += 2) {
      if (isKeyTooLarge(*it) || isValueTooLarge(*(it+1))) {
        appendError(output, "argument too large");
        return;
      }
    }
    for (auto it = command.begin() + 1; it != command.end(); it += 2) {
      db.set(*it, *(it+1));
    }

    appendSimpleString(output, "OK");
    return;
  }
  if (name == "EXPIRE" && command.size() == 3) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    auto seconds = parsePositiveInteger(command[2]);
    if (!seconds || *seconds > std::numeric_limits<long long>::max() / 1000) {
      appendError(output, "invalid expire time");
      return;
    }

    appendInteger(output, db.expire(command[1], std::chrono::milliseconds(*seconds * 1000)) ? 1 : 0);
    return;
  }
  if (name == "TTL" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    appendInteger(output, db.ttl(command[1]));
    return;
  }
  if (name == "PERSIST" && command.size() == 2) {
    if (isKeyTooLarge(command[1])) {
      appendError(output, "argument too large");
      return;
    }
    appendInteger(output, db.persist(command[1]) ? 1 : 0);
    return;
  }
  if (name == "SAVE" && command.size() == 1) {
    if (!db.saveSnapshot(dumpFile)) {
      appendError(output, "save failed");
      return;
    }
    appendSimpleString(output, "OK");
    return;
  }

  appendError(output, "unknown command");
}
