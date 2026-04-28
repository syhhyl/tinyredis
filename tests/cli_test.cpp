#include "cli.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

void testEncodeCommand() {
  assert(encodeCommand({"SET", "name", "hyl"}) == "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n");
  assert(encodeCommand({}) == "*0\r\n");
  std::cout << "PASS testEncodeCommand\n";
}

void testSplitLine() {
  assert((splitLine("SET name hyl") == std::vector<std::string>{"SET", "name", "hyl"}));
  assert((splitLine("  GET   name  ") == std::vector<std::string>{"GET", "name"}));
  assert(splitLine("   ").empty());
  std::cout << "PASS testSplitLine\n";
}

void testParseArgsDefaults() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "GET";
  char arg2[] = "name";
  char* argv[] = {arg0, arg1, arg2};
  CliOptions options;

  assert(parseArgs(3, argv, &options));
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

  assert(parseArgs(4, argv, &options));
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

  assert(!parseArgs(3, argv, &options));
  std::cout << "PASS testParseArgsRejectsInvalidPort\n";
}

void testParseArgsRejectsPartiallyParsedPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char arg2[] = "123abc";
  char* argv[] = {arg0, arg1, arg2};
  CliOptions options;

  assert(!parseArgs(3, argv, &options));
  std::cout << "PASS testParseArgsRejectsPartiallyParsedPort\n";
}

void testParseArgsRejectsMissingPort() {
  char arg0[] = "tinyredis-cli";
  char arg1[] = "-p";
  char* argv[] = {arg0, arg1};
  CliOptions options;

  assert(!parseArgs(2, argv, &options));
  std::cout << "PASS testParseArgsRejectsMissingPort\n";
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
  std::cout << "PASS all cli tests\n";
  return 0;
}
