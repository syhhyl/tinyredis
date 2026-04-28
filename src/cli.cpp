#include "cli.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

std::string encodeCommand(const std::vector<std::string>& args) {
  std::string request = "*" + std::to_string(args.size()) + "\r\n";

  for (const std::string& arg : args) {
    request += "$" + std::to_string(arg.size()) + "\r\n";
    request += arg + "\r\n";
  }

  return request;
}

std::vector<std::string> splitLine(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> args;
  std::string arg;

  while (stream >> arg) {
    args.push_back(arg);
  }

  return args;
}

bool parseArgs(int argc, char* argv[], CliOptions* options) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-p") {
      if (i + 1 >= argc) {
        std::cerr << "missing port after -p\n";
        return false;
      }

      try {
        size_t parsed = 0;
        options->port = std::stoi(argv[i + 1], &parsed);
        if (parsed != std::string(argv[i + 1]).size()) {
          std::cerr << "invalid port: " << argv[i + 1] << '\n';
          return false;
        }
      } catch (const std::exception&) {
        std::cerr << "invalid port: " << argv[i + 1] << '\n';
        return false;
      }

      ++i;
      continue;
    }

    options->commandArgs.push_back(arg);
  }

  return true;
}

namespace {

int connectServer(int port) {
  int clientFd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientFd < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << '\n';
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    std::cerr << "invalid address\n";
    close(clientFd);
    return -1;
  }

  if (connect(clientFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "connect failed: " << std::strerror(errno) << '\n';
    close(clientFd);
    return -1;
  }

  return clientFd;
}

bool sendAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = write(fd, data.data() + sent, data.size() - sent);
    if (n < 0) {
      std::cerr << "write failed: " << std::strerror(errno) << '\n';
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool readLine(int fd, std::string* line) {
  line->clear();

  while (true) {
    char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n <= 0) {
      return false;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      return true;
    }
    line->push_back(c);
  }
}

bool readBytes(int fd, size_t len, std::string* value) {
  value->clear();
  value->resize(len);

  size_t readBytes = 0;
  while (readBytes < len) {
    ssize_t n = read(fd, &(*value)[readBytes], len - readBytes);
    if (n <= 0) {
      return false;
    }
    readBytes += static_cast<size_t>(n);
  }

  std::string crlf;
  return readLine(fd, &crlf);
}

bool printResponse(int fd) {
  char type = 0;
  ssize_t n = read(fd, &type, 1);
  if (n <= 0) {
    return false;
  }

  std::string line;
  if (!readLine(fd, &line)) {
    return false;
  }

  if (type == '+') {
    std::cout << line << '\n';
    return true;
  }
  if (type == '-') {
    std::cout << line << '\n';
    return true;
  }
  if (type == ':') {
    std::cout << "(integer) " << line << '\n';
    return true;
  }
  if (type == '$') {
    int len = std::stoi(line);
    if (len < 0) {
      std::cout << "(nil)\n";
      return true;
    }

    std::string value;
    if (!readBytes(fd, static_cast<size_t>(len), &value)) {
      return false;
    }
    std::cout << value << '\n';
    return true;
  }

  std::cout << type << line << '\n';
  return true;
}

bool runCommand(int fd, const std::vector<std::string>& args) {
  std::string request = encodeCommand(args);
  return sendAll(fd, request) && printResponse(fd);
}

}  // namespace

#ifndef TINYREDIS_CLI_TEST
int main(int argc, char* argv[]) {
  CliOptions options;
  if (!parseArgs(argc, argv, &options)) {
    return 1;
  }

  int clientFd = connectServer(options.port);
  if (clientFd < 0) {
    return 1;
  }

  if (!options.commandArgs.empty()) {
    if (!runCommand(clientFd, options.commandArgs)) {
      close(clientFd);
      return 1;
    }

    close(clientFd);
    return 0;
  }

  std::string line;
  while (true) {
    std::cout << "tinyredis> ";
    if (!std::getline(std::cin, line)) {
      break;
    }

    std::vector<std::string> args = splitLine(line);
    if (args.empty()) {
      continue;
    }
    if (args[0] == "quit" || args[0] == "exit") {
      break;
    }

    if (!runCommand(clientFd, args)) {
      close(clientFd);
      return 1;
    }
  }

  close(clientFd);
  return 0;
}
#endif
