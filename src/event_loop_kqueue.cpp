#include "event_loop_backend.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

constexpr int kMaxEvents = 64;

bool applyEventChange(int backendFd, int fd, int16_t filter, uint16_t flags) {
  struct kevent event;
  EV_SET(&event, fd, filter, flags, 0, 0, nullptr);

  if (kevent(backendFd, &event, 1, nullptr, 0, nullptr) == 0) {
    return true;
  }

  if ((flags & EV_DELETE) != 0 && errno == ENOENT) {
    return true;
  }

  std::cerr << "kevent change failed: " << std::strerror(errno) << '\n';
  return false;
}

class KqueueEventLoopBackend : public EventLoopBackend {
 public:
  KqueueEventLoopBackend() {
    backendFd_ = kqueue();
    if (backendFd_ < 0) {
      std::cerr << "kqueue failed: " << std::strerror(errno) << '\n';
    }
  }

  ~KqueueEventLoopBackend() override {
    if (backendFd_ >= 0) {
      close(backendFd_);
    }
  }

  bool addRead(int fd) override {
    return valid() && applyEventChange(backendFd_, fd, EVFILT_READ, EV_ADD | EV_ENABLE);
  }

  bool setWrite(int fd, bool enabled) override {
    if (!valid()) {
      return false;
    }

    uint16_t flags = enabled ? EV_ADD | EV_ENABLE : EV_DELETE;
    return applyEventChange(backendFd_, fd, EVFILT_WRITE, flags);
  }

  void remove(int fd) override {
    if (!valid()) {
      return;
    }

    applyEventChange(backendFd_, fd, EVFILT_READ, EV_DELETE);
    applyEventChange(backendFd_, fd, EVFILT_WRITE, EV_DELETE);
  }

  std::vector<Event> wait() override {
    std::vector<Event> events;
    if (!valid()) {
      return events;
    }

    std::vector<struct kevent> rawEvents(kMaxEvents);
    int count = kevent(backendFd_, nullptr, 0, rawEvents.data(), rawEvents.size(), nullptr);
    if (count < 0) {
      if (errno != EINTR) {
        std::cerr << "kevent wait failed: " << std::strerror(errno) << '\n';
      }
      return events;
    }

    events.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      Event event;
      event.fd = static_cast<int>(rawEvents[i].ident);
      event.readable = rawEvents[i].filter == EVFILT_READ;
      event.writable = rawEvents[i].filter == EVFILT_WRITE;
      event.closed = (rawEvents[i].flags & (EV_EOF | EV_ERROR)) != 0;
      events.push_back(event);
    }

    return events;
  }
};

}  // namespace

std::unique_ptr<EventLoopBackend> createKqueueEventLoopBackend() {
  return std::make_unique<KqueueEventLoopBackend>();
}
