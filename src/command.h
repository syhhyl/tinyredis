#pragma once

#include <string>
#include <vector>
#include "database.h"

constexpr const char* kDefaultDumpFile = "dump.rdb";

std::string executeCommand(const std::vector<std::string>& command, Database& db,
                           const std::string& dumpFile = kDefaultDumpFile);
