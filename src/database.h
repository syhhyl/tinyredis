#pragma once
#include <chrono>
#include <iostream>
#include <optional>
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
  bool saveSnapshot(const std::string& path);
  bool loadSnapshot(const std::string& path);
  
  
private:
  struct Entry {
    std::string value;
    std::optional<std::chrono::system_clock::time_point> expires_at;
  };

  bool eraseIfExpired(const std::string &key);
  void eraseExpiredKeys();

  std::unordered_map<std::string, Entry> map_store_; 
};
