#pragma once
#include "database.h"

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
