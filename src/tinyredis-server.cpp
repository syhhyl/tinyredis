#include "server.h"

int main(int argc, char* argv[]) {
  ServerOptions options;
  if (!parseServerArgs(argc, argv, &options)) {
    return 1;
  }

  Server server(options.port);
  return server.run();
}
