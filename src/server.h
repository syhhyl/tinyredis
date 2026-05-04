#pragma once
#include "database.h"

#include <string>

constexpr int kDefaultServerPort = 6379;
constexpr const char* kDefaultServerDumpFile = "dump.rdb";

struct ServerOptions {
  int port = kDefaultServerPort;
  std::string dump_file = kDefaultServerDumpFile;
};

class Server {
  public:
    Server(int port, std::string dumpFile = kDefaultServerDumpFile);
    ~Server();

    int run();

  private:
    void closeListenFd();

    int port_;
    std::string dumpFile_;
    int serverFd_ = -1;
    Database db_;
};

bool parseServerArgs(int argc, char* argv[], ServerOptions* options);
