#include "command.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void testExecutePing() {
  Database db;

  assert(executeCommand({"ping"}, db) == "+PONG\r\n");
  std::cout << "PASS testExecutePing\n";
}

void testExecuteEmptyCommand() {
  Database db;

  assert(executeCommand({}, db) == "-ERR empty command\r\n");
  std::cout << "PASS testExecuteEmptyCommand\n";
}

void testExecuteCommandNameIsCaseInsensitive() {
  Database db;

  assert(executeCommand({"SeT", "name", "hyl"}, db) == "+OK\r\n");
  assert(executeCommand({"gEt", "name"}, db) == "$3\r\nhyl\r\n");
  assert(executeCommand({"ExIsTs", "name"}, db) == ":1\r\n");
  assert(executeCommand({"DeL", "name"}, db) == ":1\r\n");
  assert(executeCommand({"PiNg"}, db) == "+PONG\r\n");
  std::cout << "PASS testExecuteCommandNameIsCaseInsensitive\n";
}

void testExecuteSetGetExistsDel() {
  Database db;

  assert(executeCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(executeCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  assert(executeCommand({"exists", "name"}, db) == ":1\r\n");
  assert(executeCommand({"del", "name"}, db) == ":1\r\n");
  assert(executeCommand({"get", "name"}, db) == "$-1\r\n");
  assert(executeCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetGetExistsDel\n";
}

void testExecuteSetWithExpiration() {
  Database db;

  assert(executeCommand({"set", "name", "hyl", "ex", "1"}, db) == "+OK\r\n");
  assert(executeCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  assert(executeCommand({"get", "name"}, db) == "$-1\r\n");
  assert(executeCommand({"exists", "name"}, db) == ":0\r\n");
  assert(executeCommand({"del", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetWithExpiration\n";
}

void testExecuteSetWithInvalidExpiration() {
  Database db;

  assert(executeCommand({"set", "name", "hyl", "ex", "0"}, db) == "-ERR invalid expire time\r\n");
  assert(executeCommand({"set", "name", "hyl", "ex", "abc"}, db) == "-ERR invalid expire time\r\n");
  assert(executeCommand({"set", "name", "hyl", "px", "1"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetWithInvalidExpiration\n";
}

void testExecuteSetClearsPreviousExpiration() {
  Database db;

  assert(executeCommand({"set", "name", "hyl", "ex", "1"}, db) == "+OK\r\n");
  assert(executeCommand({"set", "name", "redis"}, db) == "+OK\r\n");

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  assert(executeCommand({"get", "name"}, db) == "$5\r\nredis\r\n");
  std::cout << "PASS testExecuteSetClearsPreviousExpiration\n";
}

void testExecuteUnknownCommand() {
  Database db;

  assert(executeCommand({"unknown"}, db) == "-ERR unknown command\r\n");
  std::cout << "PASS testExecuteUnknownCommand\n";
}

void testExecuteWrongArgumentCounts() {
  Database db;

  assert(executeCommand({"ping", "extra"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"get"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"get", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"exists"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"exists", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"del"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"del", "name", "extra"}, db) == "-ERR unknown command\r\n");
  std::cout << "PASS testExecuteWrongArgumentCounts\n";
}

void testInvalidCommandDoesNotModifyExistingValue() {
  Database db;

  assert(executeCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(executeCommand({"set", "name"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testInvalidCommandDoesNotModifyExistingValue\n";
}

void testInvalidSetDoesNotCreateValue() {
  Database db;

  assert(executeCommand({"set", "name"}, db) == "-ERR unknown command\r\n");
  assert(executeCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testInvalidSetDoesNotCreateValue\n";
}

}  // namespace

int main() {
  testExecutePing();
  testExecuteEmptyCommand();
  testExecuteCommandNameIsCaseInsensitive();
  testExecuteSetGetExistsDel();
  testExecuteSetWithExpiration();
  testExecuteSetWithInvalidExpiration();
  testExecuteSetClearsPreviousExpiration();
  testExecuteUnknownCommand();
  testExecuteWrongArgumentCounts();
  testInvalidCommandDoesNotModifyExistingValue();
  testInvalidSetDoesNotCreateValue();
  std::cout << "PASS all Command tests\n";
  return 0;
}
