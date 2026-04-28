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

void testEncodeRespValues() {
  assert(encodeSimpleString("OK") == "+OK\r\n");
  assert(encodeError("unknown command") == "-ERR unknown command\r\n");
  assert(encodeInteger(1) == ":1\r\n");
  assert(encodeBulkString("hyl") == "$3\r\nhyl\r\n");
  assert(encodeNullBulkString() == "$-1\r\n");
  std::cout << "PASS testEncodeRespValues\n";
}

}  // namespace

int main() {
  testParseCompleteCommand();
  testParseIncompleteCommand();
  testParseInvalidCommand();
  testParseKeepsRemainingInput();
  testEncodeRespValues();
  std::cout << "PASS all RESP tests\n";
  return 0;
}
