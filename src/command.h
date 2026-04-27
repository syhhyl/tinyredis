#pragma once

#include <string>
#include <unordered_map>
#include <vector>

using Store = std::unordered_map<std::string, std::string>;

std::string executeCommand(const std::vector<std::string>& command, Store* store);
