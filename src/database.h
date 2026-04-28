#pragma once
#include <iostream>
#include <unordered_map>
#include <optional>


class Database {
public:
  Database() = default;
  ~Database() = default;
  
  void set(const std::string &key, const std::string &value);
  std::optional<std::string> get(const std::string &key) const;
  bool exists(const std::string &key) const;
  bool del(const std::string &key);
  
  
private:
  std::unordered_map<std::string, std::string> map_store_; 
};