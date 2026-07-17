#include "test_config.h"

#include "server.h"

#include "resp.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>
#include <chrono>

namespace {

bool serverHarnessFailed = false;

class TempPath {
 public:
  TempPath() {
    char pattern[] = "/tmp/tinyredis-server-test-XXXXXX";
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

constexpr auto kSocketTimeout = std::chrono::seconds(1);

bool pollUntil(int fd, short events,
               std::chrono::steady_clock::time_point deadline) {
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }

    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    int timeoutMs = static_cast<int>(remaining.count());
    if (timeoutMs == 0) {
      timeoutMs = 1;
    }

    pollfd pfd{fd, events, 0};
    int rc = poll(&pfd, 1, timeoutMs);
    if (rc > 0) {
      if ((pfd.revents & POLLNVAL) != 0) {
        return false;
      }
      return (pfd.revents & (events | POLLERR | POLLHUP)) != 0;
    }
    if (rc == 0) {
      return false;
    }
    if (errno != EINTR) {
      return false;
    }
  }
}

bool writeAll(int fd, const std::string& data) {
  const auto deadline = std::chrono::steady_clock::now() + kSocketTimeout;
  size_t sent = 0;

  while (sent < data.size()) {
    if (!pollUntil(fd, POLLOUT, deadline)) {
      return false;
    }

    ssize_t n = write(fd, data.data() + sent, data.size() - sent);
    if (n > 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    return false;
  }

  return true;
}

std::string readExact(int fd, size_t len) {
  const auto deadline = std::chrono::steady_clock::now() + kSocketTimeout;
  std::string value(len, '\0');
  size_t readBytes = 0;

  while (readBytes < len) {
    if (!pollUntil(fd, POLLIN, deadline)) {
      assert(false && "timed out waiting for server response");
      return {};
    }

    ssize_t n = read(fd, &value[readBytes], len - readBytes);
    if (n > 0) {
      readBytes += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }

    assert(false && "server response ended before expected length");
    return {};
  }

  return value;
}

bool hasReadableData(int fd) {
  pollfd pfd{fd, POLLIN, 0};
  return poll(&pfd, 1, 50) > 0;
}

bool readClosed(int fd) {
  const auto deadline = std::chrono::steady_clock::now() + kSocketTimeout;

  while (pollUntil(fd, POLLIN, deadline)) {
    char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n == 0) {
      return true;
    }
    if (n < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    return false;
  }

  assert(false && "timed out waiting for server to close connection");
  return false;
}

bool readClosedOrReset(int fd) {
  const auto deadline = std::chrono::steady_clock::now() + kSocketTimeout;

  while (pollUntil(fd, POLLIN, deadline)) {
    char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n == 0 || (n < 0 && errno == ECONNRESET)) {
      return true;
    }
    if (n < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    return false;
  }

  assert(false && "timed out waiting for server to close or reset connection");
  return false;
}

int connectToServer(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  int flags = fcntl(fd, F_GETFL, 0);
  assert(flags >= 0);
  assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);

  const auto deadline = std::chrono::steady_clock::now() + kSocketTimeout;
  int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc < 0 && errno != EINPROGRESS && errno != EINTR) {
    close(fd);
    assert(false && "failed to connect to server");
    return -1;
  }

  if (rc < 0 && !pollUntil(fd, POLLOUT, deadline)) {
    close(fd);
    assert(false && "timed out connecting to server");
    return -1;
  }

  int socketError = 0;
  socklen_t socketErrorLength = sizeof(socketError);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError,
                 &socketErrorLength) < 0 ||
      socketError != 0) {
    close(fd);
    assert(false && "failed to connect to server");
    return -1;
  }

  return fd;
}

class ServerHarness {
 public:
  explicit ServerHarness(std::string dumpFile = "")
      : port_(reservePort()), dumpFile_(dumpFile.empty() ? dumpPath_.path() : std::move(dumpFile)) {
    pid_ = fork();
    assert(pid_ >= 0);

    if (pid_ == 0) {
      Server server(port_, dumpFile_);
      _exit(server.run());
    }

    waitUntilReady();
  }

  ~ServerHarness() {
    if (pid_ > 0) {
      int status = 0;
      stopAndReap(&status);
    }
  }

  int connectClient() const { return connectToServer(port_); }

  int stopWithSigterm() {
    assert(pid_ > 0);
    int status = 0;
    assert(stopAndReap(&status));
    return status;
  }

 private:
  bool stopAndReap(int* status) {
    kill(pid_, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
      pid_t rc = waitpid(pid_, status, WNOHANG);
      if (rc == pid_) {
        break;
      }
      if (rc < 0 && errno != EINTR) {
        std::cerr << "failed to reap server child: errno=" << errno << '\n';
        serverHarnessFailed = true;
        return false;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        std::cerr << "server child did not exit within 2 seconds; sending SIGKILL\n";
        serverHarnessFailed = true;
        kill(pid_, SIGKILL);
        do {
          rc = waitpid(pid_, status, 0);
        } while (rc < 0 && errno == EINTR);
        if (rc == pid_) {
          pid_ = -1;
        } else {
          std::cerr << "failed to reap killed server child: errno=" << errno << '\n';
        }
        return false;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pid_ = -1;
    if (!WIFEXITED(*status) || WEXITSTATUS(*status) != 0) {
      std::cerr << "server child exited abnormally: status=" << *status << '\n';
      serverHarnessFailed = true;
    }
    return true;
  }

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
  TempPath dumpPath_;
  std::string dumpFile_;
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

void testServerClosesTooLargeRequest() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*" + std::to_string(kMaxRespArrayLength + 1) + "\r\n"));
  assert(readExact(fd, 24) == "-ERR request too large\r\n");
  assert(readClosed(fd));

  close(fd);
  std::cout << "PASS testServerClosesTooLargeRequest\n";
}

void testServerClosesTooLargeInputBuffer() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, std::string(kMaxRespRequestBytes + 1, 'x')));
  assert(readExact(fd, 24) == "-ERR request too large\r\n");
  assert(readClosed(fd));

  close(fd);
  std::cout << "PASS testServerClosesTooLargeInputBuffer\n";
}

void testServerClosesTooLargeOutputBuffer() {
  ServerHarness harness;
  int fd = harness.connectClient();
  std::string value(kMaxRespBulkLength, 'v');
  std::string setRequest = "*3\r\n$3\r\nSET\r\n$4\r\nblob\r\n$" +
                           std::to_string(value.size()) + "\r\n" + value + "\r\n";

  assert(writeAll(fd, setRequest));
  assert(readExact(fd, 5) == "+OK\r\n");

  std::string getRequest = "*2\r\n$3\r\nGET\r\n$4\r\nblob\r\n";
  assert(writeAll(fd, getRequest + getRequest + getRequest + getRequest));
  assert(readClosed(fd));

  close(fd);
  std::cout << "PASS testServerClosesTooLargeOutputBuffer\n";
}

void testServerRespondsBeforeClosingAfterPeerHalfClose() {
  ServerHarness harness;
  int fd = harness.connectClient();

  assert(writeAll(fd, "*1\r\n$4\r\nPING\r\n"));
  assert(shutdown(fd, SHUT_WR) == 0);
  assert(readExact(fd, 7) == "+PONG\r\n");
  assert(readClosed(fd));

  close(fd);
  std::cout << "PASS testServerRespondsBeforeClosingAfterPeerHalfClose\n";
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

void testServerLoadsSnapshotOnStartup() {
  TempPath snapshot;
  Database db;
  db.set("name", "hyl");
  assert(db.saveSnapshot(snapshot.path()));

  ServerHarness harness(snapshot.path());
  int fd = harness.connectClient();

  assert(writeAll(fd, "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n"));
  assert(readExact(fd, 9) == "$3\r\nhyl\r\n");

  close(fd);
  std::cout << "PASS testServerLoadsSnapshotOnStartup\n";
}

void testServerSaveWritesSnapshot() {
  TempPath snapshot;
  {
    ServerHarness harness(snapshot.path());
    int fd = harness.connectClient();

    assert(writeAll(fd, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n"));
    assert(readExact(fd, 5) == "+OK\r\n");
    assert(writeAll(fd, "*1\r\n$4\r\nSAVE\r\n"));
    assert(readExact(fd, 5) == "+OK\r\n");

    close(fd);
  }

  Database loaded;
  assert(loaded.loadSnapshot(snapshot.path()));
  assert(loaded.get("name") == "hyl");
  std::cout << "PASS testServerSaveWritesSnapshot\n";
}

void testServerSavesSnapshotOnSigterm() {
  TempPath snapshot;
  int status = 0;
  {
    ServerHarness harness(snapshot.path());
    int fd = harness.connectClient();

    assert(writeAll(fd, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nhyl\r\n"));
    assert(readExact(fd, 5) == "+OK\r\n");
    close(fd);

    status = harness.stopWithSigterm();
  }

  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) == 0);

  Database loaded;
  assert(loaded.loadSnapshot(snapshot.path()));
  assert(loaded.get("name") == "hyl");
  std::cout << "PASS testServerSavesSnapshotOnSigterm\n";
}

void testServerRejectsConnectionsOverLimit() {
  ServerHarness harness;
  std::vector<int> clients;
  clients.reserve(kMaxServerConnections);

  for (size_t i = 0; i < kMaxServerConnections; ++i) {
    clients.push_back(harness.connectClient());
  }

  int extra = harness.connectClient();
  assert(readClosedOrReset(extra));
  close(extra);

  assert(writeAll(clients[0], "*1\r\n$4\r\nPING\r\n"));
  assert(readExact(clients[0], 7) == "+PONG\r\n");

  close(clients.back());
  clients.pop_back();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int replacement = harness.connectClient();
  assert(writeAll(replacement, "*1\r\n$4\r\nPING\r\n"));
  assert(readExact(replacement, 7) == "+PONG\r\n");
  close(replacement);

  for (int fd : clients) {
    close(fd);
  }
  std::cout << "PASS testServerRejectsConnectionsOverLimit\n";
}

void testParseServerArgsDefaults() {
  char arg0[] = "tinyredis-server";
  char* argv[] = {arg0};
  ServerOptions options;

  assert(parseServerArgs(1, argv, &options));
  assert(options.port == kDefaultServerPort);
  assert(options.dump_file == kDefaultServerDumpFile);
  std::cout << "PASS testParseServerArgsDefaults\n";
}

void testParseServerArgsPort() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--port";
  char arg2[] = "6380";
  char* argv[] = {arg0, arg1, arg2};
  ServerOptions options;

  assert(parseServerArgs(3, argv, &options));
  assert(options.port == 6380);
  std::cout << "PASS testParseServerArgsPort\n";
}

void testParseServerArgsDumpFile() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--dump-file";
  char arg2[] = "/tmp/tinyredis.rdb";
  char* argv[] = {arg0, arg1, arg2};
  ServerOptions options;

  assert(parseServerArgs(3, argv, &options));
  assert(options.dump_file == "/tmp/tinyredis.rdb");
  std::cout << "PASS testParseServerArgsDumpFile\n";
}

void testParseServerArgsRejectsMissingDumpFile() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--dump-file";
  char* argv[] = {arg0, arg1};
  ServerOptions options;

  assert(!parseServerArgs(2, argv, &options));
  std::cout << "PASS testParseServerArgsRejectsMissingDumpFile\n";
}

void testParseServerArgsRejectsInvalidPort() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--port";
  char arg2[] = "abc";
  char* argv[] = {arg0, arg1, arg2};
  ServerOptions options;

  assert(!parseServerArgs(3, argv, &options));
  std::cout << "PASS testParseServerArgsRejectsInvalidPort\n";
}

void testParseServerArgsRejectsOutOfRangePort() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--port";
  char arg2[] = "65536";
  char* argv[] = {arg0, arg1, arg2};
  ServerOptions options;

  assert(!parseServerArgs(3, argv, &options));
  std::cout << "PASS testParseServerArgsRejectsOutOfRangePort\n";
}

void testParseServerArgsRejectsMissingPort() {
  char arg0[] = "tinyredis-server";
  char arg1[] = "--port";
  char* argv[] = {arg0, arg1};
  ServerOptions options;

  assert(!parseServerArgs(2, argv, &options));
  std::cout << "PASS testParseServerArgsRejectsMissingPort\n";
}

}  // namespace

int main() {
  testParseServerArgsDefaults();
  testParseServerArgsPort();
  testParseServerArgsDumpFile();
  testParseServerArgsRejectsInvalidPort();
  testParseServerArgsRejectsOutOfRangePort();
  testParseServerArgsRejectsMissingPort();
  testParseServerArgsRejectsMissingDumpFile();
  testServerHandlesPing();
  testServerKeepsDatabaseStateOnConnection();
  testServerHandlesMultipleCommandsInOneRead();
  testServerWaitsForCompleteCommand();
  testServerClosesInvalidProtocol();
  testServerClosesTooLargeRequest();
  testServerClosesTooLargeInputBuffer();
  testServerClosesTooLargeOutputBuffer();
  testServerRespondsBeforeClosingAfterPeerHalfClose();
  testServerSharesDatabaseAcrossConnections();
  testServerLoadsSnapshotOnStartup();
  testServerSaveWritesSnapshot();
  testServerSavesSnapshotOnSigterm();
  testServerRejectsConnectionsOverLimit();
  if (serverHarnessFailed) {
    std::cerr << "FAIL ServerHarness cleanup\n";
    return 1;
  }
  std::cout << "PASS all Server tests\n";
  return 0;
}
