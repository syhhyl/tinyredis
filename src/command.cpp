#include "command.h"

namespace {

std::string toUpper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return s;
}

std::string bulkString(const std::string& value) {
  return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

}  // namespace

std::string executeCommand(const std::vector<std::string>& command, Store* store) {
  if (command.empty()) {
    return "-ERR empty command\r\n";
  }

  std::string name = toUpper(command[0]);
  if (name == "PING" && command.size() == 1) {
    return "+PONG\r\n";
  }
  if (name == "SET" && command.size() == 3) {
    (*store)[command[1]] = command[2];
    return "+OK\r\n";
  }
  if (name == "GET" && command.size() == 2) {
    auto it = store->find(command[1]);
    if (it == store->end()) {
      return "$-1\r\n";
    }
    return bulkString(it->second);
  }
  if (name == "EXISTS" && command.size() == 2) {
    return ":" + std::to_string(store->count(command[1])) + "\r\n";
  }
  if (name == "DEL" && command.size() == 2) {
    return ":" + std::to_string(store->erase(command[1])) + "\r\n";
  }

  return "-ERR unknown command\r\n";
}
