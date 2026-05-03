#include "server.h"

#include "command.h"
#include "database.h"
#include "event_loop.h"
#include "resp.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kBacklog = 128;
constexpr int kBufferSize = 4096;
constexpr int kMaxPort = 65535;

struct Connection {
  int fd = -1;
  std::string input;
  std::string output;
  bool closeAfterWrite = false;
};

bool setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    std::cerr << "fcntl F_GETFL failed: " << std::strerror(errno) << '\n';
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    std::cerr << "fcntl F_SETFL failed: " << std::strerror(errno) << '\n';
    return false;
  }

  return true;
}


void closeConnection(EventLoop& loop, std::unordered_map<int, Connection>& connections, int fd) {
  loop.remove(fd);
  close(fd);
  connections.erase(fd);
}

void processInput(Connection& connection, Database& db) {
  while (true) {
    std::vector<std::string> command;
    ParseResult result = parseRespCommand(&connection.input, &command);
    if (result == ParseResult::Incomplete) {
      return;
    }
    if (result == ParseResult::Error) {
      connection.output += encodeError("invalid protocol");
      connection.closeAfterWrite = true;
      return;
    }

    connection.output += executeCommand(command, db);
  }
}

void handleClientRead(EventLoop& loop, std::unordered_map<int, Connection>& connections,
                      int fd, Database& db) {
  auto it = connections.find(fd);
  if (it == connections.end()) {
    return;
  }

  char buffer[kBufferSize];
  bool peerClosed = false;
  while (true) {
    ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
      it->second.input.append(buffer, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      peerClosed = true;
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }

    std::cerr << "read failed: " << std::strerror(errno) << '\n';
    closeConnection(loop, connections, fd);
    return;
  }

  processInput(it->second, db);
  if (!it->second.output.empty()) {
    loop.setWrite(fd, true);
  }
  if (peerClosed) {
    if (it->second.output.empty()) {
      closeConnection(loop, connections, fd);
    } else {
      it->second.closeAfterWrite = true;
    }
  }
}

void handleClientWrite(EventLoop& loop, std::unordered_map<int, Connection>& connections, int fd) {
  auto it = connections.find(fd);
  if (it == connections.end()) {
    return;
  }

  std::string& output = it->second.output;
  while (!output.empty()) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    ssize_t n = send(fd, output.data(), output.size(), flags);
    if (n > 0) {
      output.erase(0, static_cast<size_t>(n));
      continue;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      if (errno == EPIPE || errno == ECONNRESET) {
        closeConnection(loop, connections, fd);
        return;
      }

      std::cerr << "write failed: " << std::strerror(errno) << '\n';
      closeConnection(loop, connections, fd);
      return;
    }
  }

  loop.setWrite(fd, false);
  if (it->second.closeAfterWrite) {
    closeConnection(loop, connections, fd);
  }
}

}  // namespace

bool parseServerArgs(int argc, char* argv[], ServerOptions* options) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port") {
      if (i + 1 >= argc) {
        std::cerr << "missing port after --port\n";
        return false;
      }

      try {
        size_t parsed = 0;
        int port = std::stoi(argv[i + 1], &parsed);
        if (parsed != std::string(argv[i + 1]).size() || port < 1 || port > kMaxPort) {
          std::cerr << "invalid port: " << argv[i + 1] << '\n';
          return false;
        }
        options->port = port;
      } catch (const std::exception&) {
        std::cerr << "invalid port: " << argv[i + 1] << '\n';
        return false;
      }

      ++i;
      continue;
    }

    std::cerr << "unknown option: " << arg << '\n';
    return false;
  }

  return true;
}

Server::Server(int port) : port_(port) {}

Server::~Server() {
  closeListenFd();
}

void Server::closeListenFd() {
  if (serverFd_ >= 0) {
    close(serverFd_);
    serverFd_ = -1;
  }
}

int Server::run() {
  serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd_ < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  int reuse = 1;
  if (setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "bind failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  if (listen(serverFd_, kBacklog) < 0) {
    std::cerr << "listen failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  if (!setNonBlocking(serverFd_)) {
    return 1;
  }

  EventLoop loop;
  if (!loop.valid() || !loop.addRead(serverFd_)) {
    return 1;
  }

  std::unordered_map<int, Connection> connections;

  std::cout << "tinyredis-server listening on port " << port_ << '\n';

  while (true) {
    for (const Event& event : loop.wait()) {
      if (event.fd == serverFd_ && event.readable) {
        while (true) {
          sockaddr_in clientAddr{};
          socklen_t clientLen = sizeof(clientAddr);
          int clientFd = accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
          if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            break;
          }

          if (!setNonBlocking(clientFd)) {
            close(clientFd);
            continue;
          }

#ifdef SO_NOSIGPIPE // macos bsd
          int noSigpipe = 1;
          if (setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe)) < 0) {
            std::cerr << "setsockopt SO_NOSIGPIPE failed: " << std::strerror(errno) << '\n';
            close(clientFd);
            continue;
          }
#endif

          if (!loop.addRead(clientFd)) {
            close(clientFd);
            continue;
          }

          connections.emplace(clientFd, Connection{clientFd, "", "", false});
        }
        continue;
      }

      if (event.readable) {
        handleClientRead(loop, connections, event.fd, db_);
      }
      if (event.writable) {
        handleClientWrite(loop, connections, event.fd);
      }
      if (event.closed) {
        auto it = connections.find(event.fd);
        if (it != connections.end()) {
          if (it->second.output.empty()) {
            closeConnection(loop, connections, event.fd);
          } else {
            it->second.closeAfterWrite = true;
          }
        }
      }
    }
  }

  return 0;
}
