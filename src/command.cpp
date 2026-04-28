#include "command.h"
#include "resp.h"

namespace {

std::string toUpper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return s;
}

}  // namespace

std::string executeCommand(const std::vector<std::string>& command, Database &db) {
  if (command.empty()) {
    return encodeError("empty command");
  }

  std::string name = toUpper(command[0]);
  if (name == "PING" && command.size() == 1) {
    return encodeSimpleString("PONG");
  }
  if (name == "SET" && command.size() == 3) {
    db.set(command[1], command[2]);
    return encodeSimpleString("OK");
  }
  if (name == "GET" && command.size() == 2) {
    auto value = db.get(command[1]);
    if (!value) {
      return encodeNullBulkString();
    }
    return encodeBulkString(*value);
  }
  if (name == "EXISTS" && command.size() == 2) {
    return encodeInteger(db.exists(command[1]) ? 1 : 0);
  }
  if (name == "DEL" && command.size() == 2) {
    return encodeInteger(db.del(command[1]) ? 1 : 0);
  }

  return encodeError("unknown command");
}
