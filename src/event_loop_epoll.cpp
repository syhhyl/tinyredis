#include "event_loop_backend.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstdint>

namespace {

constexpr int kMaxEvents = 64;
constexpr uint32_t kReadMask = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;

bool applyEventChange(int backendFd, int op, int fd, uint32_t events) {
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;

  if (epoll_ctl(backendFd, op, fd, &event) == 0) {
    return true;
  }

  if (op == EPOLL_CTL_DEL && errno == ENOENT) {
    return true;
  }

  std::cerr << "epoll_ctl failed: " << std::strerror(errno) << '\n';
  return false;
}

class EpollEventLoopBackend : public EventLoopBackend {
 public:
  EpollEventLoopBackend() {
    backendFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (backendFd_ < 0) {
      std::cerr << "epoll_create1 failed: " << std::strerror(errno) << '\n';
    }
  }

  bool addRead(int fd) override {
    return valid() && applyEventChange(backendFd_, EPOLL_CTL_ADD, fd, kReadMask);
  }

  bool setWrite(int fd, bool enabled) override {
    if (!valid()) {
      return false;
    }

    uint32_t mask = kReadMask;
    if (enabled) {
      mask |= EPOLLOUT;
    }
    return applyEventChange(backendFd_, EPOLL_CTL_MOD, fd, mask);
  }

  void remove(int fd) override {
    if (!valid()) {
      return;
    }

    applyEventChange(backendFd_, EPOLL_CTL_DEL, fd, 0);
  }

  std::vector<Event> wait() override {
    std::vector<Event> events;
    if (!valid()) {
      return events;
    }

    std::vector<epoll_event> rawEvents(kMaxEvents);
    int count = epoll_wait(backendFd_, rawEvents.data(), static_cast<int>(rawEvents.size()), -1);
    if (count < 0) {
      if (errno != EINTR) {
        std::cerr << "epoll_wait failed: " << std::strerror(errno) << '\n';
      }
      return events;
    }

    events.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      const uint32_t flags = rawEvents[i].events;
      Event event;
      event.fd = rawEvents[i].data.fd;
      event.readable = (flags & EPOLLIN) != 0;
      event.writable = (flags & EPOLLOUT) != 0;
      event.closed = (flags & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) != 0;
      events.push_back(event);
    }

    return events;
  }
};

}  // namespace

std::unique_ptr<EventLoopBackend> createEpollEventLoopBackend() {
  return std::make_unique<EpollEventLoopBackend>();
}
