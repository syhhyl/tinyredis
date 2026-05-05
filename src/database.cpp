#include "database.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <string>
#include <unistd.h>
#include <utility>

namespace {

constexpr char kSnapshotMagic[] = "TINYREDIS-SNAPSHOT-v1\n";
constexpr uint64_t kMaxSnapshotEntries = 1000000;
constexpr uint64_t kMaxSnapshotKeyBytes = 1024;
constexpr uint64_t kMaxSnapshotValueBytes = 1024 * 1024;

template <typename T>
bool readValue(std::istream& in, T& value) {
  in.read(reinterpret_cast<char*>(&value), sizeof(value));
  return in.good();
}

bool writeAll(int fd, const void* data, size_t size) {
  const char* bytes = static_cast<const char*>(data);
  size_t written = 0;

  while (written < size) {
    ssize_t n = write(fd, bytes + written, size - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    written += static_cast<size_t>(n);
  }

  return true;
}

template <typename T>
bool writeValueToFd(int fd, T value) {
  return writeAll(fd, &value, sizeof(value));
}

std::string parentDirectoryOf(const std::string& path) {
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

bool syncAndClose(int fd) {
  bool ok = fsync(fd) == 0;
  if (close(fd) != 0) {
    ok = false;
  }
  return ok;
}

bool fsyncParentDirectory(const std::string& path) {
  std::string dir = parentDirectoryOf(path);
  int fd = open(dir.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }

  return syncAndClose(fd);
}

int64_t toEpochMilliseconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromEpochMilliseconds(int64_t milliseconds) {
  return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
}

}  // namespace

void Database::set(const std::string& key, const std::string& value) {
  auto it = map_store_.find(key);
  if (it != map_store_.end() && it->second.expires_at) {
    expire_index_.erase(ExpireRecord{*it->second.expires_at, key});
  }

  map_store_[key] = Entry{value, std::nullopt};
}

void Database::set(const std::string& key, const std::string& value, std::chrono::milliseconds ttl) {
  auto it = map_store_.find(key);
  if (it != map_store_.end() && it->second.expires_at) {
    expire_index_.erase(ExpireRecord{*it->second.expires_at, key});
  }

  auto expiresAt = std::chrono::system_clock::now() + ttl;
  map_store_[key] = Entry{value, expiresAt};
  expire_index_.insert(ExpireRecord{expiresAt, key});
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

  auto it = map_store_.find(key);
  if (it == map_store_.end()) {
    return false;
  }

  eraseKey(it);
  return true;
}

bool Database::expire(const std::string& key, std::chrono::milliseconds ttl) {
  if (eraseIfExpired(key)) {
    return false;
  }

  auto it = map_store_.find(key);
  if (it == map_store_.end()) {
    return false;
  }

  if (it->second.expires_at) {
    expire_index_.erase(ExpireRecord{*it->second.expires_at, key});
  }

  auto expiresAt = std::chrono::system_clock::now() + ttl;
  it->second.expires_at = expiresAt;
  expire_index_.insert(ExpireRecord{expiresAt, key});
  return true;
}

long long Database::ttl(const std::string& key) {
  if (eraseIfExpired(key)) {
    return -2;
  }

  auto it = map_store_.find(key);
  if (it == map_store_.end()) {
    return -2;
  }
  if (!it->second.expires_at) {
    return -1;
  }

  auto remaining = *it->second.expires_at - std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
}

bool Database::persist(const std::string& key) {
  if (eraseIfExpired(key)) {
    return false;
  }

  auto it = map_store_.find(key);
  if (it == map_store_.end() || !it->second.expires_at) {
    return false;
  }

  expire_index_.erase(ExpireRecord{*it->second.expires_at, key});
  it->second.expires_at = std::nullopt;
  return true;
}

size_t Database::expireDue(size_t maxKeys, std::chrono::microseconds maxDuration) {
  if (maxKeys == 0 || expire_index_.empty()) {
    return 0;
  }

  auto startedAt = std::chrono::steady_clock::now();
  auto now = std::chrono::system_clock::now();
  size_t expired = 0;
  while (!expire_index_.empty() && expired < maxKeys) {
    if (std::chrono::steady_clock::now() - startedAt >= maxDuration) {
      break;
    }

    auto first = expire_index_.begin();
    if (first->expires_at > now) {
      break;
    }

    auto it = map_store_.find(first->key);
    if (it == map_store_.end()) {
      expire_index_.erase(first);
      continue;
    }

    if (!it->second.expires_at || *it->second.expires_at != first->expires_at) {
      expire_index_.erase(first);
      continue;
    }

    eraseKey(it);
    ++expired;
  }

  return expired;
}

size_t Database::size() const {
  return map_store_.size();
}

size_t Database::ttlSize() const {
  return expire_index_.size();
}

bool Database::saveSnapshot(const std::string& path) {
  eraseExpiredKeys();

  const std::string tmpPath = path + ".tmp";
  int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return false;
  }

  bool ok = true;
  ok = ok && writeAll(fd, kSnapshotMagic, sizeof(kSnapshotMagic) - 1);
  ok = ok && writeValueToFd<uint64_t>(fd, map_store_.size());

  for (const auto& [key, entry] : map_store_) {
    if (!ok) {
      break;
    }

    int64_t expiresAtMs = -1;
    if (entry.expires_at) {
      expiresAtMs = toEpochMilliseconds(*entry.expires_at);
    }

    ok = ok && writeValueToFd<uint64_t>(fd, key.size());
    ok = ok && writeValueToFd<uint64_t>(fd, entry.value.size());
    ok = ok && writeValueToFd<int64_t>(fd, expiresAtMs);
    ok = ok && writeAll(fd, key.data(), key.size());
    ok = ok && writeAll(fd, entry.value.data(), entry.value.size());
  }

  ok = syncAndClose(fd) && ok;
  if (!ok) {
    unlink(tmpPath.c_str());
    return false;
  }

  if (rename(tmpPath.c_str(), path.c_str()) != 0) {
    unlink(tmpPath.c_str());
    return false;
  }

  return fsyncParentDirectory(path);
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
  std::set<ExpireRecord> loadedExpireIndex;
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
    if (expiresAt) {
      loadedExpireIndex.insert(ExpireRecord{*expiresAt, key});
    }
  }

  if (in.peek() != std::char_traits<char>::eof()) {
    return false;
  }

  map_store_ = std::move(loaded);
  expire_index_ = std::move(loadedExpireIndex);
  return true;
}

bool Database::ExpireRecord::operator<(const ExpireRecord& other) const {
  if (expires_at != other.expires_at) {
    return expires_at < other.expires_at;
  }

  return key < other.key;
}

bool Database::eraseIfExpired(const std::string& key) {
  auto it = map_store_.find(key);
  if (it == map_store_.end() || !it->second.expires_at) {
    return false;
  }

  if (!isExpired(it->second, std::chrono::system_clock::now())) {
    return false;
  }

  eraseKey(it);
  return true;
}

bool Database::isExpired(const Entry& entry, std::chrono::system_clock::time_point now) const {
  return entry.expires_at && now >= *entry.expires_at;
}

void Database::eraseKey(std::unordered_map<std::string, Entry>::iterator it) {
  if (it->second.expires_at) {
    expire_index_.erase(ExpireRecord{*it->second.expires_at, it->first});
  }

  map_store_.erase(it);
}

void Database::eraseExpiredKeys() {
  auto now = std::chrono::system_clock::now();
  while (!expire_index_.empty()) {
    auto first = expire_index_.begin();
    if (first->expires_at > now) {
      break;
    }

    auto it = map_store_.find(first->key);
    if (it == map_store_.end()) {
      expire_index_.erase(first);
      continue;
    }

    if (!it->second.expires_at || *it->second.expires_at != first->expires_at) {
      expire_index_.erase(first);
      continue;
    }

    eraseKey(it);
  }
}
