#include "server.h"

#include "resp.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kBacklog = 128;
constexpr int kBufferSize = 4096;

void handleClient(int clientFd) {
  char buffer[kBufferSize];
  std::string input;

  while (true) {
    ssize_t n = read(clientFd, buffer, sizeof(buffer));
    if (n == 0) {
      break;
    }
    if (n < 0) {
      std::cerr << "read failed: " << std::strerror(errno) << '\n';
      break;
    }

    input.append(buffer, n);

    while (true) {
      std::vector<std::string> command;
      ParseResult result = parseRespCommand(&input, &command);
      if (result == ParseResult::Incomplete) {
        break;
      }
      if (result == ParseResult::Error) {
        const char response[] = "-ERR invalid resp\r\n";
        if (write(clientFd, response, sizeof(response) - 1) < 0) {
          std::cerr << "write failed: " << std::strerror(errno) << '\n';
        }
        return;
      }

      const char response[] = "+OK\r\n";
      if (write(clientFd, response, sizeof(response) - 1) < 0) {
        std::cerr << "write failed: " << std::strerror(errno) << '\n';
        return;
      }
    }
  }

  close(clientFd);
}

}  // namespace

Server::Server(int port) : port_(port) {}

int Server::run() {
  int serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  int reuse = 1;
  if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed: " << std::strerror(errno) << '\n';
    close(serverFd);
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "bind failed: " << std::strerror(errno) << '\n';
    close(serverFd);
    return 1;
  }

  if (listen(serverFd, kBacklog) < 0) {
    std::cerr << "listen failed: " << std::strerror(errno) << '\n';
    close(serverFd);
    return 1;
  }

  std::cout << "tinyredis listening on port " << port_ << '\n';

  while (true) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientFd < 0) {
      std::cerr << "accept failed: " << std::strerror(errno) << '\n';
      continue;
    }

    handleClient(clientFd);
  }

  close(serverFd);
  return 0;
}
