#include "command.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr size_t kMaxCommandKeyLength = 1024;
constexpr size_t kMaxCommandValueLength = 1024 * 1024;
constexpr std::chrono::milliseconds kShortTtl(500);
constexpr std::chrono::milliseconds kShortTtlWait(600);

class TempPath {
 public:
  TempPath() {
    char pattern[] = "/tmp/tinyredis-command-test-XXXXXX";
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

std::string runCommand(const std::vector<std::string>& command, Database& db,
                       const std::string& dumpFile = kDefaultDumpFile) {
  std::string output;
  appendExecuteCommand(command, db, dumpFile, output);
  return output;
}

void testExecutePing() {
  Database db;

  assert(runCommand({"ping"}, db) == "+PONG\r\n");
  assert(runCommand({"ping", "hello"}, db) == "$5\r\nhello\r\n");
  std::cout << "PASS testExecutePing\n";
}

void testExecuteEmptyCommand() {
  Database db;

  assert(runCommand({}, db) == "-ERR empty command\r\n");
  std::cout << "PASS testExecuteEmptyCommand\n";
}

void testExecuteCommandNameIsCaseInsensitive() {
  Database db;

  assert(runCommand({"SeT", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"gEt", "name"}, db) == "$3\r\nhyl\r\n");
  assert(runCommand({"ExIsTs", "name"}, db) == ":1\r\n");
  assert(runCommand({"DeL", "name"}, db) == ":1\r\n");
  assert(runCommand({"PiNg"}, db) == "+PONG\r\n");
  std::cout << "PASS testExecuteCommandNameIsCaseInsensitive\n";
}

void testExecuteSetGetExistsDel() {
  Database db;

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  assert(runCommand({"exists", "name"}, db) == ":1\r\n");
  assert(runCommand({"del", "name"}, db) == ":1\r\n");
  assert(runCommand({"get", "name"}, db) == "$-1\r\n");
  assert(runCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetGetExistsDel\n";
}

void testExecuteExistsWithMultipleKeys() {
  Database db;

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"set", "age", "21"}, db) == "+OK\r\n");
  assert(runCommand({"exists", "name", "age", "missing"}, db) == ":2\r\n");
  assert(runCommand({"exists", "missing", "other"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteExistsWithMultipleKeys\n";
}

void testExecuteSetWithExpiration() {
  Database db;

  db.set("name", "hyl", kShortTtl);
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");

  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"get", "name"}, db) == "$-1\r\n");
  assert(runCommand({"exists", "name"}, db) == ":0\r\n");
  assert(runCommand({"del", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetWithExpiration\n";
}

void testExecuteSetWithInvalidExpiration() {
  Database db;

  assert(runCommand({"set", "name", "hyl", "ex", "0"}, db) == "-ERR invalid expire time\r\n");
  assert(runCommand({"set", "name", "hyl", "ex", "abc"}, db) == "-ERR invalid expire time\r\n");
  assert(runCommand({"set", "name", "hyl", "px", "1"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteSetWithInvalidExpiration\n";
}

void testExecuteSetClearsPreviousExpiration() {
  Database db;

  db.set("name", "hyl", kShortTtl);
  assert(runCommand({"set", "name", "redis"}, db) == "+OK\r\n");

  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"get", "name"}, db) == "$5\r\nredis\r\n");
  std::cout << "PASS testExecuteSetClearsPreviousExpiration\n";
}

void testExecuteExpire() {
  Database db;

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"expire", "name", "1"}, db) == ":1\r\n");
  assert(runCommand({"expire", "missing", "1"}, db) == ":0\r\n");
  assert(runCommand({"expire", "name", "0"}, db) == "-ERR invalid expire time\r\n");
  assert(runCommand({"expire", "name", "abc"}, db) == "-ERR invalid expire time\r\n");

  assert(db.expire("name", kShortTtl));
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"get", "name"}, db) == "$-1\r\n");
  std::cout << "PASS testExecuteExpire\n";
}

void testExecuteTtl() {
  Database db;

  assert(runCommand({"ttl", "missing"}, db) == ":-2\r\n");
  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"ttl", "name"}, db) == ":-1\r\n");
  assert(runCommand({"expire", "name", "1"}, db) == ":1\r\n");

  std::string ttl = runCommand({"ttl", "name"}, db);
  assert(ttl == ":0\r\n" || ttl == ":1\r\n");

  assert(db.expire("name", kShortTtl));
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"ttl", "name"}, db) == ":-2\r\n");
  std::cout << "PASS testExecuteTtl\n";
}

void testExecutePersist() {
  Database db;

  assert(runCommand({"persist", "missing"}, db) == ":0\r\n");
  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"persist", "name"}, db) == ":0\r\n");
  assert(db.expire("name", kShortTtl));
  assert(runCommand({"persist", "name"}, db) == ":1\r\n");
  assert(runCommand({"ttl", "name"}, db) == ":-1\r\n");

  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testExecutePersist\n";
}

void testExecuteIncr() {
  Database db;

  assert(runCommand({"incr", "missing"}, db) == ":1\r\n");
  assert(runCommand({"get", "missing"}, db) == "$1\r\n1\r\n");
  assert(runCommand({"set", "count", "0"}, db) == "+OK\r\n");
  assert(runCommand({"incr", "count"}, db) == ":1\r\n");
  assert(runCommand({"get", "count"}, db) == "$1\r\n1\r\n");
  assert(runCommand({"set", "negative", "-1"}, db) == "+OK\r\n");
  assert(runCommand({"incr", "negative"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteIncr\n";
}

void testExecuteIncrRejectsInvalidInteger() {
  Database db;

  assert(runCommand({"set", "count", "abc"}, db) == "+OK\r\n");
  assert(runCommand({"incr", "count"}, db) == "-ERR value is not an integer or out of range\r\n");
  assert(runCommand({"get", "count"}, db) == "$3\r\nabc\r\n");
  std::cout << "PASS testExecuteIncrRejectsInvalidInteger\n";
}

void testExecuteIncrRejectsOverflow() {
  Database db;
  std::string max = std::to_string(std::numeric_limits<long long>::max());

  assert(runCommand({"set", "count", max}, db) == "+OK\r\n");
  assert(runCommand({"incr", "count"}, db) == "-ERR value is not an integer or out of range\r\n");
  assert(runCommand({"get", "count"}, db) ==
         "$" + std::to_string(max.size()) + "\r\n" + max + "\r\n");
  std::cout << "PASS testExecuteIncrRejectsOverflow\n";
}

void testExecuteIncrTreatsExpiredKeyAsMissing() {
  Database db;

  db.set("count", "10", kShortTtl);
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"incr", "count"}, db) == ":1\r\n");
  assert(runCommand({"get", "count"}, db) == "$1\r\n1\r\n");
  std::cout << "PASS testExecuteIncrTreatsExpiredKeyAsMissing\n";
}

void testExecuteIncrPreservesTtl() {
  Database db;

  assert(runCommand({"set", "count", "10", "ex", "1"}, db) == "+OK\r\n");
  assert(runCommand({"incr", "count"}, db) == ":11\r\n");

  std::string ttl = runCommand({"ttl", "count"}, db);
  assert(ttl == ":0\r\n" || ttl == ":1\r\n");
  assert(runCommand({"get", "count"}, db) == "$2\r\n11\r\n");
  std::cout << "PASS testExecuteIncrPreservesTtl\n";
}

void testExecuteDecr() {
  Database db;

  assert(runCommand({"decr", "missing"}, db) == ":-1\r\n");
  assert(runCommand({"get", "missing"}, db) == "$2\r\n-1\r\n");
  assert(runCommand({"set", "count", "0"}, db) == "+OK\r\n");
  assert(runCommand({"decr", "count"}, db) == ":-1\r\n");
  assert(runCommand({"get", "count"}, db) == "$2\r\n-1\r\n");
  std::cout << "PASS testExecuteDecr\n";
}

void testExecuteDecrRejectsInvalidInteger() {
  Database db;

  assert(runCommand({"set", "count", "abc"}, db) == "+OK\r\n");
  assert(runCommand({"decr", "count"}, db) == "-ERR value is not an integer or out of range\r\n");
  assert(runCommand({"get", "count"}, db) == "$3\r\nabc\r\n");
  std::cout << "PASS testExecuteDecrRejectsInvalidInteger\n";
}

void testExecuteDecrRejectsOverflow() {
  Database db;
  std::string min = std::to_string(std::numeric_limits<long long>::min());

  assert(runCommand({"set", "count", min}, db) == "+OK\r\n");
  assert(runCommand({"decr", "count"}, db) == "-ERR value is not an integer or out of range\r\n");
  assert(runCommand({"get", "count"}, db) ==
         "$" + std::to_string(min.size()) + "\r\n" + min + "\r\n");
  std::cout << "PASS testExecuteDecrRejectsOverflow\n";
}

void testExecuteDecrTreatsExpiredKeyAsMissing() {
  Database db;

  db.set("count", "10", kShortTtl);
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"decr", "count"}, db) == ":-1\r\n");
  assert(runCommand({"get", "count"}, db) == "$2\r\n-1\r\n");
  std::cout << "PASS testExecuteDecrTreatsExpiredKeyAsMissing\n";
}

void testExecuteDecrPreservesTtl() {
  Database db;

  assert(runCommand({"set", "count", "10", "ex", "1"}, db) == "+OK\r\n");
  assert(runCommand({"decr", "count"}, db) == ":9\r\n");

  std::string ttl = runCommand({"ttl", "count"}, db);
  assert(ttl == ":0\r\n" || ttl == ":1\r\n");
  assert(runCommand({"get", "count"}, db) == "$1\r\n9\r\n");
  std::cout << "PASS testExecuteDecrPreservesTtl\n";
}

void testExecuteMget() {
  Database db;

  assert(runCommand({"set", "a", "1"}, db) == "+OK\r\n");
  assert(runCommand({"set", "b", "2"}, db) == "+OK\r\n");
  assert(runCommand({"mget", "a", "b", "missing"}, db) ==
         "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$-1\r\n");
  std::cout << "PASS testExecuteMget\n";
}

void testExecuteMgetTreatsExpiredKeyAsMissing() {
  Database db;

  db.set("a", "1", kShortTtl);
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"mget", "a", "missing"}, db) == "*2\r\n$-1\r\n$-1\r\n");
  std::cout << "PASS testExecuteMgetTreatsExpiredKeyAsMissing\n";
}

void testExecuteMset() {
  Database db;

  assert(runCommand({"mset", "a", "1"}, db) == "+OK\r\n");
  assert(runCommand({"mset", "b", "2", "c", "3"}, db) == "+OK\r\n");
  assert(runCommand({"mget", "a", "b", "c"}, db) ==
         "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$1\r\n3\r\n");
  std::cout << "PASS testExecuteMset\n";
}

void testExecuteMsetClearsPreviousTtl() {
  Database db;

  db.set("a", "old", kShortTtl);
  assert(runCommand({"mset", "a", "new"}, db) == "+OK\r\n");
  std::this_thread::sleep_for(kShortTtlWait);
  assert(runCommand({"get", "a"}, db) == "$3\r\nnew\r\n");
  std::cout << "PASS testExecuteMsetClearsPreviousTtl\n";
}

void testExecuteMsetRejectsTooLargeArgumentsWithoutModifyingDatabase() {
  Database db;
  std::string key(kMaxCommandKeyLength + 1, 'k');
  std::string value(kMaxCommandValueLength + 1, 'v');

  assert(runCommand({"set", "a", "old"}, db) == "+OK\r\n");
  assert(runCommand({"mset", "a", "new", key, "value"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "a"}, db) == "$3\r\nold\r\n");
  assert(runCommand({"mset", "a", "new", "b", value}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "a"}, db) == "$3\r\nold\r\n");
  assert(runCommand({"exists", "b"}, db) == ":0\r\n");
  std::cout << "PASS testExecuteMsetRejectsTooLargeArgumentsWithoutModifyingDatabase\n";
}

void testExecuteUnknownCommand() {
  Database db;

  assert(runCommand({"unknown"}, db) == "-ERR unknown command\r\n");
  std::cout << "PASS testExecuteUnknownCommand\n";
}

void testExecuteSaveWritesSnapshot() {
  TempPath snapshot;
  Database db;

  assert(runCommand({"set", "name", "hyl"}, db, snapshot.path()) == "+OK\r\n");
  assert(runCommand({"save"}, db, snapshot.path()) == "+OK\r\n");

  Database loaded;
  assert(loaded.loadSnapshot(snapshot.path()));
  assert(loaded.get("name") == "hyl");
  std::cout << "PASS testExecuteSaveWritesSnapshot\n";
}

void testExecuteSaveReportsFailure() {
  Database db;

  assert(runCommand({"save"}, db, "/tmp") == "-ERR save failed\r\n");
  std::cout << "PASS testExecuteSaveReportsFailure\n";
}

void testExecuteWrongArgumentCounts() {
  Database db;

  assert(runCommand({"get"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"get", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"exists"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"del"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"del", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"incr"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"incr", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"decr"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"decr", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"mget"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"mset"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"mset", "name"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"mset", "name", "value", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"expire", "name"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"expire", "name", "1", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"ttl"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"ttl", "name", "extra"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"persist"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"persist", "name", "extra"}, db) == "-ERR unknown command\r\n");
  std::cout << "PASS testExecuteWrongArgumentCounts\n";
}

void testInvalidCommandDoesNotModifyExistingValue() {
  Database db;

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"set", "name"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testInvalidCommandDoesNotModifyExistingValue\n";
}

void testInvalidSetDoesNotCreateValue() {
  Database db;

  assert(runCommand({"set", "name"}, db) == "-ERR unknown command\r\n");
  assert(runCommand({"exists", "name"}, db) == ":0\r\n");
  std::cout << "PASS testInvalidSetDoesNotCreateValue\n";
}

void testSetRejectsTooLargeKeyWithoutModifyingDatabase() {
  Database db;
  std::string key(kMaxCommandKeyLength + 1, 'k');

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"set", key, "value"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  assert(runCommand({"exists", key}, db) == "-ERR argument too large\r\n");
  std::cout << "PASS testSetRejectsTooLargeKeyWithoutModifyingDatabase\n";
}

void testSetRejectsTooLargeValueWithoutModifyingDatabase() {
  Database db;
  std::string value(kMaxCommandValueLength + 1, 'v');

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"set", "name", value}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testSetRejectsTooLargeValueWithoutModifyingDatabase\n";
}

void testSetWithExpirationRejectsTooLargeArgumentsWithoutModifyingDatabase() {
  Database db;
  std::string key(kMaxCommandKeyLength + 1, 'k');
  std::string value(kMaxCommandValueLength + 1, 'v');

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"set", key, "value", "ex", "1"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"set", "name", value, "ex", "1"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testSetWithExpirationRejectsTooLargeArgumentsWithoutModifyingDatabase\n";
}

void testKeyCommandsRejectTooLargeKey() {
  Database db;
  std::string key(kMaxCommandKeyLength + 1, 'k');

  assert(runCommand({"set", "name", "hyl"}, db) == "+OK\r\n");
  assert(runCommand({"get", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"exists", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"del", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"incr", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"decr", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"mget", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"mset", key, "value"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"expire", key, "1"}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"ttl", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"persist", key}, db) == "-ERR argument too large\r\n");
  assert(runCommand({"get", "name"}, db) == "$3\r\nhyl\r\n");
  std::cout << "PASS testKeyCommandsRejectTooLargeKey\n";
}

void testAllowsMaxKeyAndValueLength() {
  Database db;
  std::string key(kMaxCommandKeyLength, 'k');
  std::string value(kMaxCommandValueLength, 'v');

  assert(runCommand({"set", key, value}, db) == "+OK\r\n");
  assert(runCommand({"exists", key}, db) == ":1\r\n");
  assert(runCommand({"get", key}, db) == "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
  assert(runCommand({"del", key}, db) == ":1\r\n");
  std::cout << "PASS testAllowsMaxKeyAndValueLength\n";
}

}  // namespace

int main() {
  testExecutePing();
  testExecuteEmptyCommand();
  testExecuteCommandNameIsCaseInsensitive();
  testExecuteSetGetExistsDel();
  testExecuteExistsWithMultipleKeys();
  testExecuteSetWithExpiration();
  testExecuteSetWithInvalidExpiration();
  testExecuteSetClearsPreviousExpiration();
  testExecuteExpire();
  testExecuteTtl();
  testExecutePersist();
  testExecuteIncr();
  testExecuteIncrRejectsInvalidInteger();
  testExecuteIncrRejectsOverflow();
  testExecuteIncrTreatsExpiredKeyAsMissing();
  testExecuteIncrPreservesTtl();
  testExecuteDecr();
  testExecuteDecrRejectsInvalidInteger();
  testExecuteDecrRejectsOverflow();
  testExecuteDecrTreatsExpiredKeyAsMissing();
  testExecuteDecrPreservesTtl();
  testExecuteMget();
  testExecuteMgetTreatsExpiredKeyAsMissing();
  testExecuteMset();
  testExecuteMsetClearsPreviousTtl();
  testExecuteMsetRejectsTooLargeArgumentsWithoutModifyingDatabase();
  testExecuteUnknownCommand();
  testExecuteSaveWritesSnapshot();
  testExecuteSaveReportsFailure();
  testExecuteWrongArgumentCounts();
  testInvalidCommandDoesNotModifyExistingValue();
  testInvalidSetDoesNotCreateValue();
  testSetRejectsTooLargeKeyWithoutModifyingDatabase();
  testSetRejectsTooLargeValueWithoutModifyingDatabase();
  testSetWithExpirationRejectsTooLargeArgumentsWithoutModifyingDatabase();
  testKeyCommandsRejectTooLargeKey();
  testAllowsMaxKeyAndValueLength();
  std::cout << "PASS all Command tests\n";
  return 0;
}
