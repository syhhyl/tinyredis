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

void testEncodeRespValues() {
  assert(encodeSimpleString("OK") == "+OK\r\n");
  assert(encodeError("unknown command") == "-ERR unknown command\r\n");
  assert(encodeInteger(1) == ":1\r\n");
  assert(encodeBulkString("hyl") == "$3\r\nhyl\r\n");
  assert(encodeNullBulkString() == "$-1\r\n");
  std::cout << "PASS testEncodeRespValues\n";
}

void testDatabaseSetGetExistsDel() {
  Database db;

  assert(!db.get("name"));
  assert(!db.exists("name"));
  assert(!db.del("name"));

  db.set("name", "hyl");
  assert(db.exists("name"));
  assert(db.get("name") == "hyl");

  db.set("name", "redis");
  assert(db.get("name") == "redis");

  assert(db.del("name"));
  assert(!db.get("name"));
  assert(!db.exists("name"));
  std::cout << "PASS testDatabaseSetGetExistsDel\n";
}

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
  testParseCompleteCommand();
  testParseIncompleteCommand();
  testParseInvalidCommand();
  testParseKeepsRemainingInput();
  testEncodeRespValues();
  testDatabaseSetGetExistsDel();
  testExecutePing();
  testExecuteEmptyCommand();
  testExecuteCommandNameIsCaseInsensitive();
  testExecuteSetGetExistsDel();
  testExecuteUnknownCommand();
  testExecuteWrongArgumentCounts();
  testInvalidCommandDoesNotModifyExistingValue();
  testInvalidSetDoesNotCreateValue();
  std::cout << "PASS all core tests\n";
  return 0;
}
