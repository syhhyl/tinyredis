#include "command.h"
#include "resp.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

void testParseCompleteCommand() {
  std::string input = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n";
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert((command == std::vector<std::string>{"SET", "name", "hyl"}));
  assert(input.empty());
  std::cout << "PASS testParseCompleteCommand\n";
}

void testParseIncompleteCommand() {
  std::string input = "*1\r\n$4\r\nPIN";
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Incomplete);
  assert(input == "*1\r\n$4\r\nPIN");
  assert(command.empty());
  std::cout << "PASS testParseIncompleteCommand\n";
}

void testParseInvalidCommand() {
  std::string input = "PING\r\n";
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Error);
  assert(command.empty());
  std::cout << "PASS testParseInvalidCommand\n";
}

void testParseKeepsRemainingInput() {
  std::string input = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert((command == std::vector<std::string>{"PING"}));
  assert(input == "*1\r\n$4\r\nPING\r\n");
  std::cout << "PASS testParseKeepsRemainingInput\n";
}

void testExecutePing() {
  Store store;

  assert(executeCommand({"ping"}, &store) == "+PONG\r\n");
  std::cout << "PASS testExecutePing\n";
}

void testExecuteSetGetExistsDel() {
  Store store;

  assert(executeCommand({"set", "name", "hyl"}, &store) == "+OK\r\n");
  assert(executeCommand({"get", "name"}, &store) == "$3\r\nhyl\r\n");
  assert(executeCommand({"exists", "name"}, &store) == ":1\r\n");
  assert(executeCommand({"del", "name"}, &store) == ":1\r\n");
  assert(executeCommand({"get", "name"}, &store) == "$-1\r\n");
  assert(executeCommand({"exists", "name"}, &store) == ":0\r\n");
  std::cout << "PASS testExecuteSetGetExistsDel\n";
}

void testExecuteUnknownCommand() {
  Store store;

  assert(executeCommand({"unknown"}, &store) == "-ERR unknown command\r\n");
  std::cout << "PASS testExecuteUnknownCommand\n";
}

}  // namespace

int main() {
  testParseCompleteCommand();
  testParseIncompleteCommand();
  testParseInvalidCommand();
  testParseKeepsRemainingInput();
  testExecutePing();
  testExecuteSetGetExistsDel();
  testExecuteUnknownCommand();
  std::cout << "PASS all core tests\n";
  return 0;
}
