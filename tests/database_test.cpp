#include "database.h"

#include <cassert>
#include <iostream>

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

}  // namespace

int main() {
  testDatabaseSetGetExistsDel();
  std::cout << "PASS all Database tests\n";
  return 0;
}
