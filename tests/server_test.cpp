#include "server.h"

#include <cassert>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

bool writeAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = write(fd, data.data() + sent, data.size() - sent);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

std::string readExact(int fd, size_t len) {
  std::string value;
  value.resize(len);

  size_t readBytes = 0;
  while (readBytes < len) {
    pollfd pfd{fd, POLLIN, 0};
    int ready = poll(&pfd, 1, 1000);
    assert(ready == 1);

    ssize_t n = read(fd, &value[readBytes], len - readBytes);
    assert(n > 0);
    readBytes += static_cast<size_t>(n);
  }

  return value;
}

bool hasReadableData(int fd) {
  pollfd pfd{fd, POLLIN, 0};
  return poll(&pfd, 1, 50) > 0;
}

bool readClosed(int fd) {
  pollfd pfd{fd, POLLIN, 0};
  int ready = poll(&pfd, 1, 1000);
  assert(ready == 1);

  char c = 0;
  return read(fd, &c, 1) == 0;
}

class ServerHarness {
 public:
  ServerHarness() {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    clientFd_ = fds[0];

    thread_ = std::thread([serverFd = fds[1]] {
      Database db;
      handleClientSession(serverFd, db);
    });
  }

  ~ServerHarness() {
    shutdown(clientFd_, SHUT_RDWR);
    close(clientFd_);
    thread_.join();
  }

  int clientFd() const { return clientFd_; }

 private:
  int clientFd_ = -1;
  std::thread thread_;
};

void testServerHandlesPing() {
  ServerHarness harness;

  assert(writeAll(harness.clientFd(), "*1\r\n$4\r\nPING\r\n"));
  assert(readExact(harness.clientFd(), 7) == "+PONG\r\n");
  std::cout << "PASS testServerHandlesPing\n";
}

void testServerKeepsDatabaseStateOnConnection() {
  ServerHarness harness;

  assert(writeAll(harness.clientFd(), "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n"));
  assert(readExact(harness.clientFd(), 5) == "+OK\r\n");

  assert(writeAll(harness.clientFd(), "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n"));
  assert(readExact(harness.clientFd(), 9) == "$3\r\nhyl\r\n");
  std::cout << "PASS testServerKeepsDatabaseStateOnConnection\n";
}

void testServerHandlesMultipleCommandsInOneRead() {
  ServerHarness harness;

  assert(writeAll(harness.clientFd(), "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n"));
  assert(readExact(harness.clientFd(), 14) == "+PONG\r\n+PONG\r\n");
  std::cout << "PASS testServerHandlesMultipleCommandsInOneRead\n";
}

void testServerWaitsForCompleteCommand() {
  ServerHarness harness;

  assert(writeAll(harness.clientFd(), "*1\r\n$4\r\nPIN"));
  assert(!hasReadableData(harness.clientFd()));

  assert(writeAll(harness.clientFd(), "G\r\n"));
  assert(readExact(harness.clientFd(), 7) == "+PONG\r\n");
  std::cout << "PASS testServerWaitsForCompleteCommand\n";
}

void testServerClosesInvalidProtocol() {
  ServerHarness harness;

  assert(writeAll(harness.clientFd(), "PING\r\n"));
  assert(readExact(harness.clientFd(), 23) == "-ERR invalid protocol\r\n");
  assert(readClosed(harness.clientFd()));
  std::cout << "PASS testServerClosesInvalidProtocol\n";
}

}  // namespace

int main() {
  testServerHandlesPing();
  testServerKeepsDatabaseStateOnConnection();
  testServerHandlesMultipleCommandsInOneRead();
  testServerWaitsForCompleteCommand();
  testServerClosesInvalidProtocol();
  std::cout << "PASS all Server tests\n";
  return 0;
}
