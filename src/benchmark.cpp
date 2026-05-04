#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

namespace {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct Options {
  std::string host = "127.0.0.1";
  int port = 6379;
  int clients = 1;
  int requests = 10000;
  int pipeline = 1;
  std::string command = "PING";
  int value_size = 64;
  std::string key_prefix = "tinyredis-benchmark";
};

struct Client {
  int fd = -1;
  std::string write_buffer;
  size_t write_offset = 0;
  std::string read_buffer;
  std::deque<TimePoint> in_flight;
};

void printUsage() {
  std::cout << "Usage: tinyredis-benchmark [options]\n"
            << "  --host HOST              default: 127.0.0.1\n"
            << "  --port PORT              default: 6379\n"
            << "  --clients N              default: 1\n"
            << "  --requests N             default: 10000\n"
            << "  --pipeline N             default: 1\n"
            << "  --command PING|GET|SET|EXISTS\n"
            << "  --value-size N           default: 64\n"
            << "  --key-prefix PREFIX      default: tinyredis-benchmark\n";
}

std::string toUpper(std::string value) {
  for (char& c : value) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return value;
}

bool parseInt(const std::string& value, int* out) {
  try {
    size_t parsed = 0;
    int result = std::stoi(value, &parsed);
    if (parsed != value.size() || result <= 0) {
      return false;
    }
    *out = result;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool parseArgs(int argc, char* argv[], Options* options) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printUsage();
      return false;
    }

    auto requireValue = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "missing value after " << name << '\n';
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--host") {
      const char* value = requireValue("--host");
      if (!value) return false;
      options->host = value;
      continue;
    }
    if (arg == "--port") {
      const char* value = requireValue("--port");
      if (!value || !parseInt(value, &options->port) || options->port > 65535) {
        std::cerr << "invalid port\n";
        return false;
      }
      continue;
    }
    if (arg == "--clients") {
      const char* value = requireValue("--clients");
      if (!value || !parseInt(value, &options->clients)) {
        std::cerr << "invalid clients\n";
        return false;
      }
      continue;
    }
    if (arg == "--requests") {
      const char* value = requireValue("--requests");
      if (!value || !parseInt(value, &options->requests)) {
        std::cerr << "invalid requests\n";
        return false;
      }
      continue;
    }
    if (arg == "--pipeline") {
      const char* value = requireValue("--pipeline");
      if (!value || !parseInt(value, &options->pipeline)) {
        std::cerr << "invalid pipeline\n";
        return false;
      }
      continue;
    }
    if (arg == "--command") {
      const char* value = requireValue("--command");
      if (!value) return false;
      options->command = toUpper(value);
      continue;
    }
    if (arg == "--value-size") {
      const char* value = requireValue("--value-size");
      if (!value || !parseInt(value, &options->value_size)) {
        std::cerr << "invalid value size\n";
        return false;
      }
      continue;
    }
    if (arg == "--key-prefix") {
      const char* value = requireValue("--key-prefix");
      if (!value) return false;
      options->key_prefix = value;
      continue;
    }

    std::cerr << "unknown option: " << arg << '\n';
    return false;
  }

  if (options->command != "PING" && options->command != "GET" &&
      options->command != "SET" && options->command != "EXISTS") {
    std::cerr << "unsupported command: " << options->command << '\n';
    return false;
  }
  return true;
}

std::string encodeCommand(const std::vector<std::string>& args) {
  std::string request = "*" + std::to_string(args.size()) + "\r\n";
  for (const std::string& arg : args) {
    request += "$" + std::to_string(arg.size()) + "\r\n";
    request += arg + "\r\n";
  }
  return request;
}

std::string makeRequest(const Options& options) {
  std::string key = options.key_prefix + ":key";
  std::string value(static_cast<size_t>(options.value_size), 'x');
  if (options.command == "PING") {
    return encodeCommand({"PING"});
  }
  if (options.command == "GET") {
    return encodeCommand({"GET", key});
  }
  if (options.command == "SET") {
    return encodeCommand({"SET", key, value});
  }
  return encodeCommand({"EXISTS", key});
}

bool setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int connectServer(const Options& options) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << '\n';
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options.port);
  if (inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "invalid host: " << options.host << '\n';
    close(fd);
    return -1;
  }

  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "connect failed: " << std::strerror(errno) << '\n';
    close(fd);
    return -1;
  }
  if (!setNonBlocking(fd)) {
    std::cerr << "failed to set non-blocking socket\n";
    close(fd);
    return -1;
  }
  return fd;
}

bool sendAllBlocking(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool readOneResponseBlocking(int fd) {
  std::string buffer;
  char chunk[4096];
  while (true) {
    ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      return false;
    }
    buffer.append(chunk, static_cast<size_t>(n));

    size_t consumed = 0;
    bool is_error = false;
    if (false) {}
    char type = buffer.empty() ? 0 : buffer[0];
    if (type == '+' || type == '-' || type == ':') {
      size_t end = buffer.find("\r\n");
      if (end != std::string::npos) return type != '-';
    } else if (type == '$') {
      size_t end = buffer.find("\r\n");
      if (end != std::string::npos) {
        int len = std::stoi(buffer.substr(1, end - 1));
        if (len < 0) return true;
        consumed = end + 2 + static_cast<size_t>(len) + 2;
        if (buffer.size() >= consumed) return true;
      }
    }
    (void)is_error;
  }
}

bool prepareDataset(const Options& options) {
  if (options.command != "GET" && options.command != "EXISTS") {
    return true;
  }

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options.port);
  if (inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1 ||
      connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return false;
  }

  std::string key = options.key_prefix + ":key";
  std::string value(static_cast<size_t>(options.value_size), 'x');
  bool ok = sendAllBlocking(fd, encodeCommand({"SET", key, value})) && readOneResponseBlocking(fd);
  close(fd);
  return ok;
}

enum class ConsumeResult {
  Complete,
  Incomplete,
  Error,
};

ConsumeResult consumeOneResponse(std::string* buffer) {
  if (buffer->empty()) {
    return ConsumeResult::Incomplete;
  }

  char type = (*buffer)[0];
  if (type == '+' || type == '-' || type == ':') {
    size_t end = buffer->find("\r\n");
    if (end == std::string::npos) {
      return ConsumeResult::Incomplete;
    }
    bool is_error = type == '-';
    buffer->erase(0, end + 2);
    return is_error ? ConsumeResult::Error : ConsumeResult::Complete;
  }

  if (type != '$') {
    return ConsumeResult::Error;
  }

  size_t end = buffer->find("\r\n");
  if (end == std::string::npos) {
    return ConsumeResult::Incomplete;
  }

  int len = 0;
  if (!parseInt(buffer->substr(1, end - 1), &len)) {
    if (buffer->substr(1, end - 1) == "-1") {
      buffer->erase(0, end + 2);
      return ConsumeResult::Complete;
    }
    return ConsumeResult::Error;
  }

  size_t total = end + 2 + static_cast<size_t>(len) + 2;
  if (buffer->size() < total) {
    return ConsumeResult::Incomplete;
  }
  if ((*buffer)[total - 2] != '\r' || (*buffer)[total - 1] != '\n') {
    return ConsumeResult::Error;
  }
  buffer->erase(0, total);
  return ConsumeResult::Complete;
}

void fillPipeline(Client* client, const std::string& request, const Options& options,
                  int* sent_requests) {
  while (*sent_requests < options.requests &&
         static_cast<int>(client->in_flight.size()) < options.pipeline) {
    client->write_buffer += request;
    client->in_flight.push_back(Clock::now());
    ++(*sent_requests);
  }
}

double toMs(std::chrono::nanoseconds value) {
  return static_cast<double>(value.count()) / 1000000.0;
}

double percentile(std::vector<double>& values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  size_t index = static_cast<size_t>((p / 100.0) * static_cast<double>(values.size() - 1));
  return values[index];
}

bool runBenchmark(const Options& options) {
  if (!prepareDataset(options)) {
    std::cerr << "failed to prepare dataset\n";
    return false;
  }

  std::vector<Client> clients(static_cast<size_t>(options.clients));
  for (Client& client : clients) {
    client.fd = connectServer(options);
    if (client.fd < 0) {
      return false;
    }
  }

  const std::string request = makeRequest(options);
  std::vector<double> latencies;
  latencies.reserve(static_cast<size_t>(options.requests));

  int sent_requests = 0;
  int completed_requests = 0;
  TimePoint started = Clock::now();

  while (completed_requests < options.requests) {
    for (Client& client : clients) {
      fillPipeline(&client, request, options, &sent_requests);
    }

    std::vector<pollfd> fds;
    fds.reserve(clients.size());
    for (const Client& client : clients) {
      short events = POLLIN;
      if (client.write_offset < client.write_buffer.size()) {
        events |= POLLOUT;
      }
      fds.push_back(pollfd{client.fd, events, 0});
    }

    int ready = poll(fds.data(), fds.size(), 1000);
    if (ready <= 0) {
      std::cerr << "poll failed or timed out\n";
      return false;
    }

    for (size_t i = 0; i < clients.size(); ++i) {
      Client& client = clients[i];
      if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        std::cerr << "connection closed during benchmark\n";
        return false;
      }

      if ((fds[i].revents & POLLOUT) && client.write_offset < client.write_buffer.size()) {
        ssize_t n = send(client.fd, client.write_buffer.data() + client.write_offset,
                         client.write_buffer.size() - client.write_offset, 0);
        if (n > 0) {
          client.write_offset += static_cast<size_t>(n);
          if (client.write_offset == client.write_buffer.size()) {
            client.write_buffer.clear();
            client.write_offset = 0;
          }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          std::cerr << "write failed: " << std::strerror(errno) << '\n';
          return false;
        }
      }

      if (fds[i].revents & POLLIN) {
        char chunk[4096];
        while (true) {
          ssize_t n = recv(client.fd, chunk, sizeof(chunk), 0);
          if (n > 0) {
            client.read_buffer.append(chunk, static_cast<size_t>(n));
            continue;
          }
          if (n == 0) {
            std::cerr << "server closed connection\n";
            return false;
          }
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          std::cerr << "read failed: " << std::strerror(errno) << '\n';
          return false;
        }

        while (true) {
          ConsumeResult result = consumeOneResponse(&client.read_buffer);
          if (result == ConsumeResult::Incomplete) {
            break;
          }
          if (result == ConsumeResult::Error || client.in_flight.empty()) {
            std::cerr << "invalid response\n";
            return false;
          }
          TimePoint sent_at = client.in_flight.front();
          client.in_flight.pop_front();
          latencies.push_back(toMs(std::chrono::duration_cast<std::chrono::nanoseconds>(
              Clock::now() - sent_at)));
          ++completed_requests;
        }
      }
    }
  }

  TimePoint finished = Clock::now();
  for (Client& client : clients) {
    close(client.fd);
  }

  double elapsed_seconds = std::chrono::duration<double>(finished - started).count();
  double sum = 0.0;
  for (double latency : latencies) {
    sum += latency;
  }
  std::sort(latencies.begin(), latencies.end());

  std::cout << std::fixed << std::setprecision(3)
            << "command: " << options.command << '\n'
            << "clients: " << options.clients << '\n'
            << "requests: " << options.requests << '\n'
            << "pipeline: " << options.pipeline << '\n'
            << "value_size: " << options.value_size << "B\n"
            << "qps: " << static_cast<double>(completed_requests) / elapsed_seconds << '\n'
            << "avg_ms: " << sum / static_cast<double>(latencies.size()) << '\n'
            << "p95_ms: " << percentile(latencies, 95.0) << '\n'
            << "p99_ms: " << percentile(latencies, 99.0) << '\n';
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  if (!parseArgs(argc, argv, &options)) {
    return 1;
  }
  return runBenchmark(options) ? 0 : 1;
}
