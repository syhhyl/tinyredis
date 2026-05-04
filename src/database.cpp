#include "database.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>

namespace {

constexpr char kSnapshotMagic[] = "TINYREDIS-SNAPSHOT-v1\n";
constexpr uint64_t kMaxSnapshotEntries = 1000000;
constexpr uint64_t kMaxSnapshotKeyBytes = 1024;
constexpr uint64_t kMaxSnapshotValueBytes = 1024 * 1024;

template <typename T>
bool writeValue(std::ostream& out, T value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(value));
  return out.good();
}

template <typename T>
bool readValue(std::istream& in, T& value) {
  in.read(reinterpret_cast<char*>(&value), sizeof(value));
  return in.good();
}

int64_t toEpochMilliseconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromEpochMilliseconds(int64_t milliseconds) {
  return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
}

}  // namespace

void Database::set(const std::string& key, const std::string& value) {
  map_store_[key] = Entry{value, std::nullopt};
}

void Database::set(const std::string& key, const std::string& value, std::chrono::milliseconds ttl) {
  map_store_[key] = Entry{value, std::chrono::system_clock::now() + ttl};
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

bool Database::saveSnapshot(const std::string& path) {
  eraseExpiredKeys();

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out.write(kSnapshotMagic, sizeof(kSnapshotMagic) - 1);
  if (!out.good() || !writeValue<uint64_t>(out, map_store_.size())) {
    return false;
  }

  for (const auto& [key, entry] : map_store_) {
    int64_t expiresAtMs = -1;
    if (entry.expires_at) {
      expiresAtMs = toEpochMilliseconds(*entry.expires_at);
    }

    if (!writeValue<uint64_t>(out, key.size()) ||
        !writeValue<uint64_t>(out, entry.value.size()) ||
        !writeValue<int64_t>(out, expiresAtMs)) {
      return false;
    }

    out.write(key.data(), static_cast<std::streamsize>(key.size()));
    out.write(entry.value.data(), static_cast<std::streamsize>(entry.value.size()));
    if (!out.good()) {
      return false;
    }
  }

  return out.good();
}

bool Database::loadSnapshot(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return true;
  }

  char magic[sizeof(kSnapshotMagic) - 1];
  in.read(magic, sizeof(magic));
  if (!in.good() || std::string(magic, sizeof(magic)) != std::string(kSnapshotMagic, sizeof(kSnapshotMagic) - 1)) {
    return false;
  }

  uint64_t entryCount = 0;
  if (!readValue<uint64_t>(in, entryCount) || entryCount > kMaxSnapshotEntries) {
    return false;
  }

  std::unordered_map<std::string, Entry> loaded;
  const auto now = std::chrono::system_clock::now();
  for (uint64_t i = 0; i < entryCount; ++i) {
    uint64_t keySize = 0;
    uint64_t valueSize = 0;
    int64_t expiresAtMs = -1;
    if (!readValue<uint64_t>(in, keySize) ||
        !readValue<uint64_t>(in, valueSize) ||
        !readValue<int64_t>(in, expiresAtMs) ||
        keySize > kMaxSnapshotKeyBytes ||
        valueSize > kMaxSnapshotValueBytes ||
        keySize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
        valueSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
      return false;
    }

    std::string key(keySize, '\0');
    std::string value(valueSize, '\0');
    if (!key.empty()) {
      in.read(&key[0], static_cast<std::streamsize>(key.size()));
    }
    if (!value.empty()) {
      in.read(&value[0], static_cast<std::streamsize>(value.size()));
    }
    if (!in.good()) {
      return false;
    }

    std::optional<std::chrono::system_clock::time_point> expiresAt;
    if (expiresAtMs >= 0) {
      expiresAt = fromEpochMilliseconds(expiresAtMs);
      if (*expiresAt <= now) {
        continue;
      }
    }

    loaded[key] = Entry{value, expiresAt};
  }

  if (in.peek() != std::char_traits<char>::eof()) {
    return false;
  }

  map_store_ = std::move(loaded);
  return true;
}

bool Database::eraseIfExpired(const std::string& key) {
  auto it = map_store_.find(key);
  if (it == map_store_.end() || !it->second.expires_at) {
    return false;
  }

  if (std::chrono::system_clock::now() < *it->second.expires_at) {
    return false;
  }

  map_store_.erase(it);
  return true;
}

void Database::eraseExpiredKeys() {
  for (auto it = map_store_.begin(); it != map_store_.end();) {
    if (it->second.expires_at && std::chrono::system_clock::now() >= *it->second.expires_at) {
      it = map_store_.erase(it);
    } else {
      ++it;
    }
  }
}
