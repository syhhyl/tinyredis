#include "command.h"

#include <cassert>
#include <iostream>
#include <string>
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
  testExecuteUnknownCommand();
  testExecuteWrongArgumentCounts();
  testInvalidCommandDoesNotModifyExistingValue();
  testInvalidSetDoesNotCreateValue();
  std::cout << "PASS all Command tests\n";
  return 0;
}
