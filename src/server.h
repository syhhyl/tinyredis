#pragma once

class Server {
  public:
    Server(int port);

    int run();

  private:
    int port_;
};
