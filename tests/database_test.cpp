#include "database.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

class TempPath {
 public:
  TempPath() {
    char pattern[] = "/tmp/tinyredis-database-test-XXXXXX";
    int fd = mkstemp(pattern);
    assert(fd >= 0);
    close(fd);
    unlink(pattern);
    path_ = pattern;
  }

  ~TempPath() { unlink(path_.c_str()); }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

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

void testDatabaseSnapshotSavesAndLoadsValues() {
  TempPath snapshot;
  Database saved;
  saved.set("name", "hyl");
  saved.set("cache", "redis", std::chrono::milliseconds(1000));

  assert(saved.saveSnapshot(snapshot.path()));

  Database loaded;
  assert(loaded.loadSnapshot(snapshot.path()));
  assert(loaded.get("name") == "hyl");
  assert(loaded.get("cache") == "redis");
  std::cout << "PASS testDatabaseSnapshotSavesAndLoadsValues\n";
}

void testDatabaseSnapshotSkipsExpiredKeysOnSave() {
  TempPath snapshot;
  Database saved;
  saved.set("expired", "value", std::chrono::milliseconds(10));
  saved.set("alive", "value");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(saved.saveSnapshot(snapshot.path()));

  Database loaded;
  assert(loaded.loadSnapshot(snapshot.path()));
  assert(!loaded.get("expired"));
  assert(loaded.get("alive") == "value");
  std::cout << "PASS testDatabaseSnapshotSkipsExpiredKeysOnSave\n";
}

void testDatabaseLoadMissingSnapshotAsEmptyDatabase() {
  TempPath snapshot;
  Database loaded;

  assert(loaded.loadSnapshot(snapshot.path()));
  assert(!loaded.exists("name"));
  std::cout << "PASS testDatabaseLoadMissingSnapshotAsEmptyDatabase\n";
}

void testDatabaseRejectsInvalidSnapshot() {
  TempPath snapshot;
  {
    std::ofstream out(snapshot.path(), std::ios::binary | std::ios::trunc);
    out << "invalid";
  }

  Database loaded;
  loaded.set("name", "hyl");
  assert(!loaded.loadSnapshot(snapshot.path()));
  assert(loaded.get("name") == "hyl");
  std::cout << "PASS testDatabaseRejectsInvalidSnapshot\n";
}

}  // namespace

int main() {
  testDatabaseSetGetExistsDel();
  testDatabaseExpiresKeys();
  testDatabaseSetClearsPreviousExpiration();
  testDatabaseSnapshotSavesAndLoadsValues();
  testDatabaseSnapshotSkipsExpiredKeysOnSave();
  testDatabaseLoadMissingSnapshotAsEmptyDatabase();
  testDatabaseRejectsInvalidSnapshot();
  std::cout << "PASS all Database tests\n";
  return 0;
}
