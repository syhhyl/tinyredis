#pragma once

#include <string>
#include <vector>
#include "database.h"

std::string executeCommand(const std::vector<std::string>& command, Database &db);
