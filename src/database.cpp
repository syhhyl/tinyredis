#include "database.h"

void Database::set(const std::string& key, const std::string& value) {
  map_store_[key] = value;
}

std::optional<std::string> Database::get(const std::string &key) const {
  auto it = map_store_.find(key);
  if (it == map_store_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool Database::exists(const std::string& key) const {
  return map_store_.find(key) != map_store_.end();
}

bool Database::del(const std::string& key) {
  return map_store_.erase(key) > 0;
}