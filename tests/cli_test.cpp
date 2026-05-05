#include "cli.h"

#include <cassert>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

void writeAll(int fd, const std::string& data) {
  size_t written = 0;
  while (written < data.size()) {
    ssize_t n = write(fd, data.data() + written, data.size() - written);
    assert(n > 0);
    written += static_cast<size_t>(n);
  }
}

std::string readAll(int fd) {
  std::string output;
  char buffer[256];
  while (true) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    output.append(buffer, static_cast<size_t>(n));
  }
  return output;
}

std::string capturePrintResponse(const std::string& response) {
  int input[2];
  int output[2];
  assert(pipe(input) == 0);
  assert(pipe(output) == 0);

  writeAll(input[1], response);
  close(input[1]);

  std::cout.flush();
  int savedStdout = dup(STDOUT_FILENO);
  assert(savedStdout >= 0);
  assert(dup2(output[1], STDOUT_FILENO) >= 0);
  close(output[1]);

  assert(printResponse(input[0]));
  std::cout.flush();

  assert(dup2(savedStdout, STDOUT_FILENO) >= 0);
  close(savedStdout);
  close(input[0]);

  std::string printed = readAll(output[0]);
  close(output[0]);
  return printed;
}

void testEncodeCommand() {
  assert(encodeCommand({"SET", "name", "hyl"}) == "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n");
  assert(encodeCommand({}) == "*0\r\n");
  std::cout << "PASS testEncodeCommand\n";
}

void testSplitLine() {
  assert((splitLine("SET name hyl") == std::vector<std::string>{"SET", "name", "hyl"}));
  assert((splitLine("  GET   name  ") == std::vector<std::string>{"GET", "name"}));
  assert((splitLine("SET msg \"hello world\"") == std::vector<std::string>{"SET", "msg", "hello world"}));
  assert((splitLine("SET empty \"\"") == std::vector<std::string>{"SET", "empty", ""}));
  assert(splitLine("   ").empty());
  std::cout << "PASS testSplitLine\n";
}

void testParseArgsDefaults() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "GET";
  char arg2[] = "name";
  char* argv[] = {arg0, arg1, arg2};
  CliOptions options;

  assert(parseArgs(3, argv, options));
  assert(options.port == kDefaultCliPort);
  assert((options.commandArgs == std::vector<std::string>{"GET", "name"}));
  std::cout << "PASS testParseArgsDefaults\n";
}

void testParseArgsPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char arg2[] = "6380";
  char arg3[] = "PING";
  char* argv[] = {arg0, arg1, arg2, arg3};
  CliOptions options;

  assert(parseArgs(4, argv, options));
  assert(options.port == 6380);
  assert((options.commandArgs == std::vector<std::string>{"PING"}));
  std::cout << "PASS testParseArgsPort\n";
}

void testParseArgsRejectsInvalidPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char arg2[] = "abc";
  char* argv[] = {arg0, arg1, arg2};
  CliOptions options;

  assert(!parseArgs(3, argv, options));
  std::cout << "PASS testParseArgsRejectsInvalidPort\n";
}

void testParseArgsRejectsPartiallyParsedPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char arg2[] = "123abc";
  char* argv[] = {arg0, arg1, arg2};
  CliOptions options;

  assert(!parseArgs(3, argv, options));
  std::cout << "PASS testParseArgsRejectsPartiallyParsedPort\n";
}

void testParseArgsRejectsMissingPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char* argv[] = {arg0, arg1};
  CliOptions options;

  assert(!parseArgs(2, argv, options));
  std::cout << "PASS testParseArgsRejectsMissingPort\n";
}

void testPrintResponseArray() {
  assert(capturePrintResponse("*3\r\n$2\r\n21\r\n$3\r\nsyh\r\n$-1\r\n") ==
         "1) \"21\"\n2) \"syh\"\n3) (nil)\n");
  std::cout << "PASS testPrintResponseArray\n";
}

void testPrintResponseNestedArray() {
  assert(capturePrintResponse("*2\r\n*2\r\n$1\r\na\r\n$1\r\nb\r\n:3\r\n") ==
         "1) 1) \"a\"\n   2) \"b\"\n2) (integer) 3\n");
  std::cout << "PASS testPrintResponseNestedArray\n";
}

}  // namespace

int main() {
  testEncodeCommand();
  testSplitLine();
  testParseArgsDefaults();
  testParseArgsPort();
  testParseArgsRejectsInvalidPort();
  testParseArgsRejectsPartiallyParsedPort();
  testParseArgsRejectsMissingPort();
  testPrintResponseArray();
  testPrintResponseNestedArray();
  std::cout << "PASS all cli tests\n";
  return 0;
}
