#include "database.h"

void Database::set(const std::string& key, const std::string& value) {
  map_store_[key] = Entry{value, std::nullopt};
}

void Database::set(const std::string& key, const std::string& value, std::chrono::milliseconds ttl) {
  map_store_[key] = Entry{value, std::chrono::steady_clock::now() + ttl};
}

std::optional<std::string> Database::get(const std::string &key) {
  if (eraseIfExpired(key)) {
    return std::nullopt;
  }

  auto it = map_store_.find(key);
  if (it == map_store_.end()) {
    return std::nullopt;
  }
  return it->second.value;
}

bool Database::exists(const std::string& key) {
  if (eraseIfExpired(key)) {
    return false;
  }

  return map_store_.find(key) != map_store_.end();
}

bool Database::del(const std::string& key) {
  if (eraseIfExpired(key)) {
    return false;
  }

  return map_store_.erase(key) > 0;
}

bool Database::eraseIfExpired(const std::string& key) {
  auto it = map_store_.find(key);
  if (it == map_store_.end() || !it->second.expires_at) {
    return false;
  }

  if (std::chrono::steady_clock::now() < *it->second.expires_at) {
    return false;
  }

  map_store_.erase(it);
  return true;
}
