#pragma once
#include <chrono>
#include <iostream>
#include <optional>
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
  
  
private:
  struct Entry {
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> expires_at;
  };

  bool eraseIfExpired(const std::string &key);

  std::unordered_map<std::string, Entry> map_store_; 
};
