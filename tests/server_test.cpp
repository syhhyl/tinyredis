#include "server.h"

#include <arpa/inet.h>
#include <cassert>
#include <csignal>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

int reservePort() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  assert(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

  socklen_t len = sizeof(addr);
  assert(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
  int port = ntohs(addr.sin_port);

  close(fd);
  return port;
}

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

int connectToServer(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);

  assert(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  return fd;
}

class ServerHarness {
 public:
  ServerHarness() : port_(reservePort()) {
    pid_ = fork();
    assert(pid_ >= 0);

    if (pid_ == 0) {
      Server server(port_);
      _exit(server.run());
    }

    waitUntilReady();
  }

  ~ServerHarness() {
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      waitpid(pid_, nullptr, 0);
    }
  }

  int connectClient() const { return connectToServer(port_); }

 private:
  void waitUntilReady() const {
    for (int i = 0; i < 100; ++i) {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      assert(fd >= 0);

      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port_);
      assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);

      if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        close(fd);
        return;
      }

      close(fd);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(false && "server did not start listening");
  }

  int port_ = 0;
  pid_t pid_ = -1;
};

void testServerHandlesPing() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*1\r\n$4\r\nPING\r\n"));
  assert(readExact(fd, 7) == "+PONG\r\n");

  close(fd);
  std::cout << "PASS testServerHandlesPing\n";
}

void testServerKeepsDatabaseStateOnConnection() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n"));
  assert(readExact(fd, 5) == "+OK\r\n");

  assert(writeAll(fd, "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n"));
  assert(readExact(fd, 9) == "$3\r\nhyl\r\n");

  close(fd);
  std::cout << "PASS testServerKeepsDatabaseStateOnConnection\n";
}

void testServerHandlesMultipleCommandsInOneRead() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n"));
  assert(readExact(fd, 14) == "+PONG\r\n+PONG\r\n");

  close(fd);
  std::cout << "PASS testServerHandlesMultipleCommandsInOneRead\n";
}

void testServerWaitsForCompleteCommand() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*1\r\n$4\r\nPIN"));
  assert(!hasReadableData(fd));

  assert(writeAll(fd, "G\r\n"));
  assert(readExact(fd, 7) == "+PONG\r\n");

  close(fd);
  std::cout << "PASS testServerWaitsForCompleteCommand\n";
}

void testServerClosesInvalidProtocol() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "PING\r\n"));
  assert(readExact(fd, 23) == "-ERR invalid protocol\r\n");
  assert(readClosed(fd));

  close(fd);
  std::cout << "PASS testServerClosesInvalidProtocol\n";
}

void testServerSharesDatabaseAcrossConnections() {
  ServerHarness harness;
  int first = harness.connectClient();
  int second = harness.connectClient();

  assert(writeAll(first, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n"));
  assert(readExact(first, 5) == "+OK\r\n");

  assert(writeAll(second, "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n"));
  assert(readExact(second, 9) == "$3\r\nhyl\r\n");

  close(first);
  close(second);
  std::cout << "PASS testServerSharesDatabaseAcrossConnections\n";
}

}  // namespace

int main() {
  testServerHandlesPing();
  testServerKeepsDatabaseStateOnConnection();
  testServerHandlesMultipleCommandsInOneRead();
  testServerWaitsForCompleteCommand();
  testServerClosesInvalidProtocol();
  testServerSharesDatabaseAcrossConnections();
  std::cout << "PASS all Server tests\n";
  return 0;
}
