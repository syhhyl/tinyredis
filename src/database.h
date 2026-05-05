#pragma once
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>


class Database {
public:
  Database() = default;
  ~Database() = default;
  
  void set(const std::string &key, const std::string &value);
  void set(const std::string &key, const std::string &value, std::chrono::milliseconds ttl);
  std::optional<std::string> get(const std::string &key);
  bool exists(const std::string &key);
  bool del(const std::string &key);
  bool expire(const std::string& key, std::chrono::milliseconds ttl);
  long long ttl(const std::string& key);
  bool persist(const std::string& key);
  size_t expireDue(size_t maxKeys, std::chrono::microseconds maxDuration);
  size_t size() const;
  size_t ttlSize() const;
  bool saveSnapshot(const std::string& path);
  bool loadSnapshot(const std::string& path);
  
  
private:
  struct Entry {
    std::string value;
    std::optional<std::chrono::system_clock::time_point> expires_at;
  };

  struct ExpireRecord {
    std::chrono::system_clock::time_point expires_at;
    std::string key;

    bool operator<(const ExpireRecord& other) const;
  };

  bool eraseIfExpired(const std::string &key);
  bool isExpired(const Entry& entry, std::chrono::system_clock::time_point now) const;
  void eraseKey(std::unordered_map<std::string, Entry>::iterator it);
  void eraseExpiredKeys();

  std::unordered_map<std::string, Entry> map_store_; 
  std::set<ExpireRecord> expire_index_;
};
