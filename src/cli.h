#pragma once

#include <string>
#include <vector>

constexpr int kDefaultCliPort = 6379;

struct CliOptions {
  int port = kDefaultCliPort;
  std::vector<std::string> commandArgs;
};

std::string encodeCommand(const std::vector<std::string>& args);
std::vector<std::string> splitLine(const std::string& line);
bool parseArgs(int argc, char* argv[], CliOptions* options);
