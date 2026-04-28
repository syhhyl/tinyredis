#pragma once
#include "database.h"

void handleClientSession(int clientFd, Database& db);

class Server {
  public:
    Server(int port);
    ~Server() = default;

    int run();

  private:
    void handleClient(int clientFd);

    int port_;
    Database db_;
};
