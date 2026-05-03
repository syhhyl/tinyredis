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

void testParseRejectsTooLargeArray() {
  std::string input = "*" + std::to_string(kMaxRespArrayLength + 1) + "\r\n";
  std::string original = input;
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::TooLarge);
  assert(input == original);
  assert(command.empty());
  std::cout << "PASS testParseRejectsTooLargeArray\n";
}

void testParseRejectsTooLargeBulk() {
  std::string input = "*1\r\n$" + std::to_string(kMaxRespBulkLength + 1) + "\r\n";
  std::string original = input;
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::TooLarge);
  assert(input == original);
  assert(command.empty());
  std::cout << "PASS testParseRejectsTooLargeBulk\n";
}

void testParseRejectsTooLargeRequest() {
  std::string bulk(kMaxRespBulkLength, 'x');
  std::string input = "*5\r\n";
  for (int i = 0; i < 5; ++i) {
    input += "$" + std::to_string(kMaxRespBulkLength) + "\r\n" + bulk + "\r\n";
  }
  std::string original = input;
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::TooLarge);
  assert(input == original);
  assert(command.empty());
  std::cout << "PASS testParseRejectsTooLargeRequest\n";
}

void testParseDoesNotCountPipelinedBytesAsOneRequest() {
  std::string input = "*1\r\n$4\r\nPING\r\n" + std::string(kMaxRespRequestBytes + 1, 'x');
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert((command == std::vector<std::string>{"PING"}));
  assert(input == std::string(kMaxRespRequestBytes + 1, 'x'));
  std::cout << "PASS testParseDoesNotCountPipelinedBytesAsOneRequest\n";
}

void testParseAllowsMaxArrayLength() {
  std::string input = "*" + std::to_string(kMaxRespArrayLength) + "\r\n";
  for (size_t i = 0; i < kMaxRespArrayLength; ++i) {
    input += "$1\r\nx\r\n";
  }
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert(command.size() == kMaxRespArrayLength);
  assert(input.empty());
  std::cout << "PASS testParseAllowsMaxArrayLength\n";
}

void testParseAllowsMaxBulkLength() {
  std::string bulk(kMaxRespBulkLength, 'x');
  std::string input = "*1\r\n$" + std::to_string(kMaxRespBulkLength) + "\r\n" + bulk + "\r\n";
  std::vector<std::string> command;

  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert((command == std::vector<std::string>{bulk}));
  assert(input.empty());
  std::cout << "PASS testParseAllowsMaxBulkLength\n";
}

void testParseAllowsMaxRequestBytes() {
  std::string input = "*4\r\n";
  for (int i = 0; i < 3; ++i) {
    std::string bulk(kMaxRespBulkLength, 'x');
    input += "$" + std::to_string(bulk.size()) + "\r\n" + bulk + "\r\n";
  }

  size_t remaining = kMaxRespRequestBytes - input.size();
  size_t lastBulkLen = remaining - std::string("$1048529\r\n\r\n").size();
  std::string lastBulk(lastBulkLen, 'x');
  input += "$" + std::to_string(lastBulk.size()) + "\r\n" + lastBulk + "\r\n";
  std::vector<std::string> command;

  assert(input.size() == kMaxRespRequestBytes);
  assert(parseRespCommand(&input, &command) == ParseResult::Complete);
  assert(command.size() == 4);
  assert(input.empty());
  std::cout << "PASS testParseAllowsMaxRequestBytes\n";
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
  testParseRejectsTooLargeArray();
  testParseRejectsTooLargeBulk();
  testParseRejectsTooLargeRequest();
  testParseDoesNotCountPipelinedBytesAsOneRequest();
  testParseAllowsMaxArrayLength();
  testParseAllowsMaxBulkLength();
  testParseAllowsMaxRequestBytes();
  testEncodeRespValues();
  std::cout << "PASS all RESP tests\n";
  return 0;
}
