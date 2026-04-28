#pragma once
#include "Database.h"

class Server {
  public:
    Server(int port);
    ~Server() = default;

    int run();
    void handleClient(int clientFd);

  private:
    int port_;
    Database db_;
};
