#pragma once

#include <string>
#include <vector>
#include "Database.h"

std::string executeCommand(const std::vector<std::string>& command, Database &db);
