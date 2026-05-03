#include "database.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

void testDatabaseSetGetExistsDel() {
  Database db;

  assert(!db.get("name"));
  assert(!db.exists("name"));
  assert(!db.del("name"));

  db.set("name", "hyl");
  assert(db.exists("name"));
  assert(db.get("name") == "hyl");

  db.set("name", "redis");
  assert(db.get("name") == "redis");

  assert(db.del("name"));
  assert(!db.get("name"));
  assert(!db.exists("name"));
  std::cout << "PASS testDatabaseSetGetExistsDel\n";
}

void testDatabaseExpiresKeys() {
  Database db;

  db.set("name", "hyl", std::chrono::milliseconds(10));
  assert(db.exists("name"));
  assert(db.get("name") == "hyl");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(!db.get("name"));
  assert(!db.exists("name"));
  assert(!db.del("name"));
  std::cout << "PASS testDatabaseExpiresKeys\n";
}

void testDatabaseSetClearsPreviousExpiration() {
  Database db;

  db.set("name", "hyl", std::chrono::milliseconds(10));
  db.set("name", "redis");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(db.exists("name"));
  assert(db.get("name") == "redis");
  std::cout << "PASS testDatabaseSetClearsPreviousExpiration\n";
}

}  // namespace

int main() {
  testDatabaseSetGetExistsDel();
  testDatabaseExpiresKeys();
  testDatabaseSetClearsPreviousExpiration();
  std::cout << "PASS all Database tests\n";
  return 0;
}
