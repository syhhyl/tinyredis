#pragma once
#include "database.h"

constexpr int kDefaultServerPort = 6379;

struct ServerOptions {
  int port = kDefaultServerPort;
};

class Server {
  public:
    Server(int port);
    ~Server();

    int run();

  private:
    void closeListenFd();

    int port_;
    int serverFd_ = -1;
    Database db_;
};

bool parseServerArgs(int argc, char* argv[], ServerOptions* options);
